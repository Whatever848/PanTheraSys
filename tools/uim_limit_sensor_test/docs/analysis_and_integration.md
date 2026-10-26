# 现有项目分析与集成边界

## PanTheraSys 技术栈

- 语言标准：C++20。
- UI：Qt 6.2 Widgets。
- 构建：CMake 3.21+，当前现场工具链为 Qt 6.2 MinGW 8.1 64 位。
- 界面语言：中文；主色为深蓝黑背景、青色标题、蓝色操作按钮、绿色通过和红色失败。
- 配置：INI/QSettings 风格；现有主程序使用 `config/defaults.ini`。

## 现有 USB-CAN 与 UIM 代码

PanTheraSys 主工程中的生产封装位于：

```text
src/adapters/uim/uim_motor_gateway.h
src/adapters/uim/uim_motor_gateway.cpp
```

该封装使用 `QLibrary` 动态加载 `UISimCanFunc.dll`，已经实现：

- 搜索、打开和关闭 UIROBOT 网关；
- 获取节点列表和型号信息；
- 轮询 `UimSFBK` 得到 S1/S2/S3 当前电平；
- 读取或写入传感器消抖和动作配置；
- 电机使能、运动、位置、电流等控制。

设备监控页已经有基于轮询快照的水箱上下限位显示逻辑：

```text
src/modules/dashboard/device_monitor_page.cpp
DeviceMonitorPage::handleWaterTankLevelSensors(...)
```

它把节点 6 的 S3 用作上限位、节点 7 的 S3 用作下限位，但当前逻辑直接解释为“正常/报警”，不提供原始 CAN RTCN、事件码、人工触发/恢复测试状态机或可导出的报文日志。

## 已有协议能力与缺口

主工程的 `UimMotorGateway` 没有绑定 SDK 的 `UIMRegRtcnCallBack`，因此不能把未经加工的 `CAN_MSG_OBJ` 交给测试 UI。厂商 SDK 包中存在：

```text
UISimCanFunc_RT.h
LoadSimCanFunc.cpp
UIMVCDemoDlg.cpp::RevRtcnNotify(...)
```

这些文件确认了回调 ABI、CAN 帧结构、节点 ID 位运算，以及 S3 在 CAN ID 低 8 位中的 RTCN 命令值。

手册事件码 `0xA4/0xA5` 与 USB-CAN 回调中的 `0x73/0xF3` 属于不同表示层，不能把 `0xA4/0xA5` 当作完整 CAN 帧或直接假定它位于 CAN 数据区。

## 本次采用的文件边界

用户最终要求“创建新项目，不要修改现有 PanTheraSys”，因此没有扩展现有设备监控页，也没有改动根 CMake。所有实现均位于：

```text
tools/uim_limit_sensor_test/
```

独立工具复用了相同技术栈、SDK 动态加载方式、命名风格和 UI 配色，但故意没有链接主工程的 `UimMotorGateway`：该类同时暴露运动和配置写入函数，并且缺少原始 RTCN 回调。独立工具只声明安全监听所需的五个 DLL 函数，缩小了误调用边界。

## 仍缺少或需要现场确认的资料

- 节点 6/7 是否为最终地址；工具允许修改，不会写回驱动器。
- 现场驱动器是 UIM24204、UIM24202P 还是其他变体，以及固件版本。
- S3/AG 开关的常态电平和实际触发方向。
- 当前 MCFG 的 S3IE 与 S34CON 是否会产生 RTCN；工具不会主动读取或修改。
- `dwMsgLen` 在现场 SDK 版本中是否始终为单帧通知。当前处理方式与厂商 VC 示例一致：每次回调读取一个 `CAN_MSG_OBJ`，同时把 `dwMsgLen` 保留在内存帧对象中。
- USB-CAN SDK 是否允许多进程共享。按现有主工程的占用提示，真实测试前应关闭 PanTheraConsole。

