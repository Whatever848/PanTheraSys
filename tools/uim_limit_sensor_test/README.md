# PanTheraSys 液位上下限位传感器测试工具

这是一个独立 CMake 项目，不加入、不修改 PanTheraSys 主工程。它沿用主工程的 C++20、Qt 6.2 Widgets、MinGW 和深色界面风格，专门用于两台 UIM242 系列驱动器的 S3 液位限位人工测试。

## 安全边界

- 默认且始终只接收 RTCN 回调。
- 只动态绑定 `SearchGateway`、`OpenGateway`、`CloseGateway`、`UIMRegRtcnCallBack` 和 `GetLastErr`。
- 未绑定电机使能、速度、电流、位置、回零、数字输出、寄存器写入或 EEPROM API。
- 不读取、不写入 MCFG、SCFG、S34CON，也不会自动启用 S3 通知。
- 工具与 PanTheraConsole 不应同时打开同一个 USB-CAN 网关。

`OpenGateway` 会返回 SDK 已发现的节点列表；本项目没有另行实现驱动器扫描协议。

## 协议解析依据

附件《UIROBOT-UIM242XX-SERIES-MANUAL(1).pdf》将 S3 下降沿、上升沿事件标识为 `0xA4`、`0xA5`。这些值是协议事件标识，不是完整 CAN ID。

厂商随 SDK 提供的 VC 示例 `RevRtcnNotify()` 对 `CAN_MSG_OBJ` 的解析规则为：

```text
nodeId = ((CAN_ID >> 3) & 0x60) + ((CAN_ID >> 19) & 0x1F)
CAN_RCW = CAN_ID & 0xFF
CAN_RCW 0x73 = UIM242 S3 falling edge
CAN_RCW 0xF3 = UIM242 S3 rising edge
```

因此界面同时显示两层信息：

- 下降沿：手册事件码 `0xA4`，USB-CAN 回调 RCW `0x73`。
- 上升沿：手册事件码 `0xA5`，USB-CAN 回调 RCW `0xF3`。

原始 CAN ID、帧类型、DLC 和数据字节均原样保留。解析器不会在数据区中搜索 `0xA4/0xA5`，也不会把事件码伪装成完整 CAN 帧。

## 构建

在本目录运行：

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

可执行文件位于：

```text
build/mingw/UimLimitSensorTest.exe
```

也可双击本目录的 `launch_uim_limit_sensor_test.cmd` 启动。仓库根目录的
`launch_pantherasys.cmd` 启动的是 PanTheraConsole 主程序，不会打开本测试工具。

CMake 会在现有路径找到 DLL 时，将 `D:/PanSoftware/UIMDemo/UISimCanFunc.dll` 复制到可执行文件目录。若现场路径不同，可在界面中重新选择 DLL。

## 监听状态与诊断日志

“打开并监听”成功只说明网关已打开且 RTCN 回调注册调用未报错，不代表现场已经产生或收到 CAN 事件。状态区会明确区分：

- `注册调用返回 0 / 等待首帧验证`：SDK 调用已完成，但尚无硬件帧进入应用，不能据此认定回调链路已工作。
- `注册调用已完成 / N 秒无硬件帧`：持续没有硬件回调；这不是“已监听到传感器”。
- `已收到硬件帧：N`：至少一个真实硬件回调已进入应用，后面同时显示最近接收时间或空闲秒数。

连接区分别展示 Windows 已登记的串口和 SDK 网关信息。对于 `USBC9100` 这类
`UIDEV_USBCAN` 设备，SDK 可能返回 `uiComIndex=0`；这不是 Windows 的 COM0，
也不会作为 `OpenGateway` 的参数。程序实际使用 SDK 返回的 `DeviceIndex` 打开网关，
因此界面会显示“USB直连（SDK COM字段=0，不用于打开网关）”，并在下一行单独列出
Windows 的 COM3、COM4 等端口供现场核对。

RTCN 注册遵循与当前 64 位 DLL 配套的 VC 示例：
`UIMRegRtcnCallBack(0, deviceIndex, callback)`。这里的第一个参数固定为 `0`，
不使用 `SearchGateway` 返回的 `dwDevType`。现场 64 位 DLL 实际返回 `0`，而配套示例
不检查该返回值，因此程序只把 `0xFFFFFFFF` 视为明确失败；返回 `0` 时会继续等待，
并且只有收到首个真实硬件帧后才确认 SDK 到应用的接收链路正常。

界面下半部始终显示“监听诊断日志”，按顺序记录：

1. 实际加载的 DLL 路径、大小、修改时间和 SHA-256。
2. `SearchGateway`、`OpenGateway`、`UIMRegRtcnCallBack` 的输入及返回值。
3. 每个回调的 DeviceIndex、回调长度、CAN ID、IDE、RTR、SendType、DLC 和 DATA。
4. CAN ID 节点位域、解析出的节点、RCW、S3 事件及路由决定。
5. 测试开始、触发、恢复、通过、超时及分层排查结论。

超时排查按实际观测分层：完全没有硬件回调时检查通道占用、通知配置、DLL 和接线；有回调但无目标节点时检查驱动器 ID；有目标节点但没有 `0x73/0xF3` 时保留原始报文并标记协议待确认；只收到一个边沿时检查开关是否恢复。保存测试日志会同时导出原始报文表和诊断日志。

## 人工测试步骤

1. 关闭可能占用同一 USB-CAN 网关的 PanTheraConsole 或厂商调试软件。
2. 启动 `UimLimitSensorTest.exe`。
3. 确认 SDK DLL 路径，点击“刷新 USB-CAN 通道”。
4. 选择通道，点击“打开并监听”。
5. 核对节点列表及上限位 ID（默认 6）、下限位 ID（默认 7）。
6. 点击“开始上限位测试”，手动触发上限位开关，再恢复开关。
7. 两个相反的 S3 边沿均来自节点 6 后，上限位测试显示通过。
8. 对下限位节点 7 重复相同步骤。
9. 点击“保存测试日志”导出带 UTF-8 BOM 的 CSV。

测试不预设“触发”究竟对应高电平还是低电平。第一次收到的边沿记作触发，随后必须收到同一目标节点的相反边沿才判定恢复并通过。其他节点事件不会推进当前测试。

## 模拟验证

无需连接硬件即可点击四个模拟按钮。推荐流程：

1. 开始上限位测试。
2. 点击“上限位 S3 下降沿”。
3. 点击“上限位 S3 上升沿”。
4. 确认结果通过、时间记录完整、表格中来源为“模拟”。

模拟通过只证明解析器和 UI 流程通过，不代表真实硬件测试通过。

## 仍需现场确认

- 上限位与下限位实际节点 ID 是否为 6、7。
- 驱动器实际型号及固件版本是 UIM24204、UIM24202P 或其他型号。
- S3 与 AG 的实际接线、电气常态及动作方向。
- MCFG 中 S3 状态变化通知是否启用，以及 S34CON 是否允许产生 RTCN。
- 当前 SDK/DLL 与现场 USB-CAN 硬件版本是否完全匹配。

若超时未收到事件，工具只给出检查提示，不会尝试写入任何配置。

