# 水下清洁机器人智能监控系统

基于 Qt 5.15.2 Widgets、Qt WebEngine、qmake 和 QPainter 的经典电光蓝海洋科技监控界面框架。

## 构建

1. 使用 Qt Creator 打开 `UnderwaterMonitor.pro`。
2. 选择 Qt 5.15.2 MSVC2019 64-bit Kit（需要安装 Qt WebEngine 组件）。
3. 执行 qmake，然后构建并运行。

项目使用 `third_party/opencv` 内的 OpenCV 4.5.3（MSVC vc15 x64）接入 USB 摄像头，
不依赖本机 `D:\opencv` 绝对路径。qmake 会按 Debug/Release 自动链接对应的
`opencv_world453[d].lib`，并在链接完成后把 `opencv_world453[d].dll` 和
`opencv_videoio_msmf453_64[d].dll` 复制到可执行文件目录。

也可以在 VS2019 x64 开发者命令行中执行：

```powershell
D:\Qt\5.15.2\msvc2019_64\bin\qmake.exe UnderwaterMonitor.pro
nmake release
release\UnderwaterMonitor.exe --windowed
```

## 高德地图本机配置

路径规划页使用 `QWebEngineView + 高德地图 JS API 2.0` 显示地图，不依赖桌面瓦片 URL，也不要求先提供路径算法。

1. 将 `amap.local.ini.example` 复制为与 `UnderwaterMonitor.exe` 同目录的 `amap.local.ini`。
2. 在 `[AMap]` 中填写高德 Web 端 JS API 的 `Key` 和对应的 `SecurityJsCode`。
3. `InitialLongitude`、`InitialLatitude` 和 `InitialZoom` 是地图立即显示时使用的后备视图。

`amap.local.ini` 已被 Git 忽略，不要将 Key 写入源码或上传到 GitHub。程序优先读取程序目录中的完整配置；文件缺失或字段不完整时，使用同一组凭据的内置混淆后备，并在地图状态中标注配置来源。混淆只能避免明文显示，不能替代高德控制台的域名/IP/调用量限制。程序启动后会在后台预热地图，并使用高德 `AMap.Geolocation` 获取后备位置。高德会按当前电脑环境自动选择可用定位方式；没有 GPS 的台式机可能只能获得城市级位置。

地图不提供手动模式切换，当前统一使用高德地图页面显示境内外底图；任务、遥测和下发坐标在程序内部统一使用 GCJ-02。

程序预留外部 GPS 位置接口，优先级为 GPS → 高德 → 最后有效位置。GPS 超过 10 秒没有更新时自动降级到高德定位；本版本不扫描串口或连接具体 GPS 硬件，未来只需将设备适配器输出的 `LocationFix` 传给 `MainWindow::updateGpsLocation()`。

## 操作

- 点击顶部按钮切换五个页面。
- `Left` / `Right` 或 `PageUp` / `PageDown` 翻页。
- `F11` 切换全屏和窗口模式。
- 全屏时按 `Esc` 回到窗口模式。
- 调试时可使用 `UnderwaterMonitor.exe --windowed` 以窗口模式启动。
- 使用 `--smoke-test --windowed` 可在约 1.2 秒后自动退出，用于启动回归检查。

## 界面与扩展接口

- 全局采用经典电光蓝、冰青高光和少量极光紫渐变。
- 仪表盘具有共享 30 FPS 的冷凝雾、轨道流光、等离子能量弧和粒子脉冲特效。
- 全局视觉特效默认以 0.5 倍速度播放，保持 30 FPS 平滑度并减少快速闪烁感。
- 健康监控页提供八个推进器仪表，采用两列四行布局并保留紧凑状态曲线。
- 路径规划页采用 Qt WebEngine 高德地图双层视图：完整地图负责最终展示，隐藏的清理地图只保留背景水域色并生成紫蓝色半透明覆盖层；单目标设置、安全校验、设备起点和 Hybrid A* 规划统一使用清理地图的水域掩膜。项目已注册内置 Hybrid A* 规划适配器，真实设备协议仍通过固定接口接入。
- 视频监控页使用独立工作线程打开 USB 摄像头 0，优先 DirectShow、失败后回退 Media Foundation；请求 MJPG 1920×1080/30 FPS，不支持时使用设备实际规格。切出视频页后继续采集，返回时显示最新画面。
- 三维声纳页提供声纳三维点云的主题化接口待接入视图，当前不伪造点云、目标或探测结果，也不引入 OpenGL 依赖。
- 数据管理页支持会话遥测缓存、选中行或全部数据 CSV 导出、采集显示参数持久化，以及设备控制参数请求接口。
- 普通页面切换为约 250 ms 的淡入与轻微横移；路径页使用约 150 ms 的轻量遮罩过渡，避免对 WebEngine 整页离屏合成。
- 所有业务按钮通过 `ActionId` 和 `MainWindow::actionRequested(ActionId)` 预留统一接口。

河道任务页只保留真实 Hybrid A* 与真实 UDP 接口，不再包含模拟规划器、模拟设备或临时 JSON 协议。当前已接入设备心跳与 GCJ-02 位置/艏向遥测；路线与控制下发协议尚未提供，因此不会发送任何设备控制报文。USB 摄像头画面已通过 OpenCV 实际接入。

## 可直接运行版本

构建后的独立运行目录位于 `dist/UnderwaterMonitor`。部署时需要包含 MSVC 运行库、Qt WebEngine 运行库、`QtWebEngineProcess.exe`、resources、translations、`opencv_world453.dll` 和 `opencv_videoio_msmf453_64.dll`；不要只复制单独的 EXE。OpenCV Debug DLL、PDB、示例程序、FFmpeg DLL 和 CMake 配置不进入正式运行包。

正式运行包不部署 `position/qtposition_winrt.dll`、`qtposition_positionpoll.dll` 或 `qtposition_serialnmea.dll`。定位由高德 JS API 及未来自定义 GPS 接口提供；排除 Qt Windows 原生定位插件可避免 Qt 5.15.2 的 `qtposition_winrt` 回调崩溃路径。
# 河道任务闭环（首版）

路径规划页现在提供当前地图视口内的单目标任务流程：正常模式使用 UDP 设备遥测作为起点；协议接入前也可勾选“使用手动测试设备位置”，设置测试艏向后点击“在地图标记测试设备”，再在地图上标记真实测试起点。用户直接单击地图即可设置或替换目标。水域识别生成覆盖层与规划约束，路线由工作线程中的规划器生成；在真实协议接入前只准备任务数据，不发送报文。

当前河道识别基于隐藏清理地图截图：先移除道路、文字和建筑，再逐像素匹配 RGB(178,206,254) 标记水域。完整地图只用于展示，程序内部统一使用清理地图掩膜和 GCJ-02 坐标。

- UDP ACK 超时为 800 ms，最多重试 3 次；遥测超过 5 秒即锁定规划和上传。水域识别会生成 5 米离岸安全范围：设备图标自动显示在同一连通水域内的安全起点；目标不安全时保留原目标并显示黄色推荐点，确认采用后再作为安全终点。
- 手动测试位置与 UDP 遥测互斥：勾选测试模式后，UDP 仍可接收但不会覆盖测试起点；取消勾选后恢复最新 UDP 位置。每次改变测试位置或艏向都会作废旧路线，下一次规划重新采集当前视口水域。
- 默认已注册设备 UDP 协议：仅接受 `106.55.38.224` 发往本机 `54545` 的心跳与 GCJ-02 位置/艏向报文，并对心跳回复 `ACK`。程序启动后每秒发送 `55 55 FF FF` 心跳；收到设备心跳后建立连接。`上传路线` 发送一帧 `0x01` 巡线路线，`原路返航` 发送一帧 `0x02` 返航路线，`停止任务` 发送 `0x00` 空路径帧。每帧是 `55 55 | 总字节数(uint16 大端) | 指令 | 经度/纬度点列(均为 GCJ-02×10^7、大端) | 前述字节累加校验 | FF FF`。该协议未定义设备 ACK，界面只报告本机 UDP 已发出，实际执行以随后收到的设备遥测为准。规划成功后可通过 `RoutePage::preparedMission()` 读取任务快照，也会发出 `missionPrepared(const PreparedMission &)` 信号。
- 当前项目通过 `SuppliedHybridAstarBridge` 调用 `third_party/hybrid_astar/main(1).cpp` 中的原始 `planGeoHybridAstar()`。该算法源文件保持原样，不直接加入工程源文件列表；桥接层只负责将当前水域边界闭合、按约 4 米间距加密为障碍物点，并把结果转换回项目路线。原算法内部约 5 米障碍物避让半径继续生效，项目会在返回后再次检查整条路线是否处于确认水域内。
- 内部任务、遥测和下发路线全部使用 GCJ-02；地图点击、绘制、水域边界和规划桥接层不再做 GCJ-02/WGS-84 转换。
- 颜色识别严格依赖当前高德清理地图中的 RGB(178,206,254)，不做补洞、断口连接、形态学平滑或颜色容差；它不等同于电子海图或测深数据，识别和目标校验由程序自动完成。
