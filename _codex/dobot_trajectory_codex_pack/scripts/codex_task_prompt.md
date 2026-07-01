# 给 Codex 的命令 / 提示词

请在本地项目 `D:\PanSoftware\PanTheraSys` 中实现 DOBOT 轨迹复现文件同步与运行功能。当前压缩包中已经提供 Qt/C++ 参考实现，请按下面要求集成，而不是重写一套无关逻辑。

## 背景

机械臂厂家软件 DobotStudio Pro 4.6 安装目录是：

```text
D:\PanSoftware\DOBOT\DobotStudio Pro 4.6
```

用户在厂家软件中录制轨迹文件后，轨迹 `.csv` 实际保存在机械臂控制器 SFTP 目录：

```text
/dobot/userdata/project/process/trajectory/
```

厂家源码里 UI 使用 `/developOnly/process/trajectory`，底层映射到上面的真实控制器目录。不要扫描厂家安装目录，也不要扫描 PanTheraSys 本地目录来获得轨迹列表。

控制器连接信息：

```text
controllerIp = UI 中填写的控制器 IP，当前常用值 192.168.5.1
sftpPort = 22
sftpUser = root
sftpPassword = dobot
dashboardPort = 29999
```

DOBOT TCP 轨迹复现接口：

```text
StartPath(traceName,isConst,multi,sample,freq,user,tool)
```

运行时只传文件名，不传完整路径。例如：

```text
StartPath(2026-06-04-19-13-15.csv,isConst=0,multi=1.00,sample=50,freq=0.200)
```

## 任务

1. 打开并分析 `D:\PanSoftware\PanTheraSys` 的 Qt/C++ 项目结构，判断项目使用 CMake 还是 qmake。
2. 找到图片 2 中“机械臂控制 / 预设轨迹 / 刷新 / 轨迹下拉框 / 运行轨迹”的 QWidget、Dialog 或 MainWindow 代码位置。
3. 把压缩包中的以下文件集成到项目中，建议放到 `src/dobot_trajectory/`：
   - `DobotSftpClient.h/.cpp`
   - `DobotTrajectoryManager.h/.cpp`
   - `DobotTrajectoryRunner.h/.cpp`
   - 如项目没有可复用的 29999 TCP 客户端，再集成 `DobotDashboardClient.h/.cpp`
4. 给项目加入 libssh2 依赖。Qt 没有原生 SFTP，不能只用 QFile/QDir。Windows/CMake 推荐 vcpkg：

```powershell
vcpkg install libssh2:x64-windows
```

5. 在刷新按钮槽函数中调用：

```cpp
m_trajectoryManager->setRobotIp(controllerIp);
m_trajectoryManager->refreshComboBox(ui->comboTrajectory, &err);
```

6. 在运行按钮槽函数中读取当前下拉框文件名，并调用：

```cpp
DobotStartPathOptions options;
options.isConst = 0;
options.multi = 1.0;
options.sample = 50;
options.freq = 0.2;
m_trajectoryRunner->startPath(fileName, options, &reply);
```

7. 如果项目已经有 dashboardSend/发送 29999 指令函数，`DobotTrajectoryRunner` 必须复用它。不要再开第二个 29999 长连接，除非项目当前没有 Dashboard 客户端。
8. 保证 QString 到 TCP 发送使用 UTF-8，支持中文轨迹文件名。
9. 刷新列表排序要和厂家一致：普通文件在前，时间格式 `yyyy-MM-dd-HH-mm-ss.csv` 在后且时间倒序。
10. 加日志：刷新成功显示文件数量；刷新失败显示 SFTP 错误；StartPath 失败显示原始 reply。
11. 编译项目并修复所有编译错误。不要删除原有机械臂连接、使能、拖拽、Z轴对齐功能。

## 验收

- 厂家软件中新增录制 `xxx.csv` 后，PanTheraSys 点击“刷新”可以看到该文件。
- 下拉框数量与厂家软件轨迹复现列表一致。
- 选择该文件点击运行，发送的是 `StartPath(文件名.csv,...)`，不是本地路径或控制器完整路径。
- Dashboard 返回 `0,...` 时显示运行成功。
- `RobotMode()` 运行中为 7，完成后回到 5；如果报警，日志输出 GetErrorID 或原始错误。

请直接修改项目文件，并在最后列出你改动的文件、依赖安装方式、编译命令、以及一次手动测试步骤。
