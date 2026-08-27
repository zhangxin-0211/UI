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
2. 在 `[AMap]` 中填写高德 Web 端 JS API 的 `JsApiKey` 和对应的 `SecurityJsCode`。
3. `InitialLongitude`、`InitialLatitude` 和 `InitialZoom` 是地图立即显示时使用的后备视图。

`amap.local.ini` 已被 Git 忽略，不要将 Key 写入源码或上传到 GitHub。程序启动后会在后台预热地图，并使用高德 `AMap.Geolocation` 获取后备位置。高德会按当前电脑环境自动选择可用定位方式；没有 GPS 的台式机可能只能获得城市级位置。

地图不提供手动模式切换：高德覆盖区域始终显示真实高德底图；境外或高德加载失败时由程序自动使用全球经纬度网格，继续显示 WGS-84 位置、路径点和顺序连线，不再把高德地图移动到无底图区域。

程序预留外部 GPS 位置接口，优先级为 GPS → 高德 → 最后有效位置。GPS 超过 10 秒没有更新时自动降级到高德定位；本版本不扫描串口或连接具体 GPS 硬件，未来只需将设备适配器输出的 `LocationFix` 传给 `MainWindow::updateGpsLocation()`。

## 操作

- 点击顶部按钮切换四个页面。
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
- 路径规划页采用 Qt WebEngine 高德地图主屏，支持缩放、拖动、路径点编辑和机器人位置显示；路径算法仍为预留接口。
- 视频监控页使用独立工作线程打开 USB 摄像头 0，优先 DirectShow、失败后回退 Media Foundation；请求 MJPG 1920×1080/30 FPS，不支持时使用设备实际规格。切出视频页后继续采集，返回时显示最新画面。
- 三维声纳页提供声纳三维点云的主题化接口待接入视图，当前不伪造点云、目标或探测结果，也不引入 OpenGL 依赖。
- 数据管理页支持会话遥测缓存、选中行或全部数据 CSV 导出、采集显示参数持久化，以及设备控制参数请求接口。
- 普通页面切换为约 250 ms 的淡入与轻微横移；路径页使用约 150 ms 的轻量遮罩过渡，避免对 WebEngine 整页离屏合成。
- 所有业务按钮通过 `ActionId` 和 `MainWindow::actionRequested(ActionId)` 预留统一接口。

当前遥测数据和控制命令仍为演示/接口预留；USB 摄像头画面已通过 OpenCV 实际接入。

## 可直接运行版本

构建后的独立运行目录位于 `dist/UnderwaterMonitor`。部署时需要包含 MSVC 运行库、Qt WebEngine 运行库、`QtWebEngineProcess.exe`、resources、translations、`opencv_world453.dll` 和 `opencv_videoio_msmf453_64.dll`；不要只复制单独的 EXE。OpenCV Debug DLL、PDB、示例程序、FFmpeg DLL 和 CMake 配置不进入正式运行包。
