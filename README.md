# 水下清洁机器人智能监控系统

基于 Qt 5.15.2 Widgets、qmake 和 QPainter 的经典电光蓝海洋科技监控界面框架。

## 构建

1. 使用 Qt Creator 打开 `UnderwaterMonitor.pro`。
2. 选择 Qt 5.15.2 MinGW Kit。
3. 执行 qmake，然后构建并运行。

也可以在 Qt 5.15.2 MinGW 命令行中执行：

```powershell
qmake UnderwaterMonitor.pro
mingw32-make -j4
release\UnderwaterMonitor.exe
```

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
- 路径规划页采用统一地图规划主屏，预留高德地图、地图标点和路径规划算法数据接口；当前不接入真实地图或算法。
- 数据管理页支持会话遥测缓存、选中行或全部数据 CSV 导出、采集显示参数持久化，以及设备控制参数请求接口。
- 页面切换为约 250 ms 的淡入与轻微横移过渡。
- 所有业务按钮通过 `ActionId` 和 `MainWindow::actionRequested(ActionId)` 预留统一接口。

当前数据、视频和控制命令均为演示占位实现，后续可通过替换 `DemoDataModel` 接入真实设备。

## 可直接运行版本

构建后的独立运行目录位于 `dist/UnderwaterMonitor`。该目录包含 Qt、MinGW 和图形平台运行库，可直接整体复制到其他 Windows 电脑运行；不要只复制单独的 EXE。
