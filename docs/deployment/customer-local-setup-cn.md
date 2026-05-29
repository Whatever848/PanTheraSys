# PanTheraSys 客户本地电脑部署与配置说明

更新日期：2026-05-29

## 1. 说明范围

本文用于指导将当前 PanTheraSys 项目转移到客户本地 Windows 电脑，并让项目能够正常启动、演示和验证。

当前项目是 Qt 6.2.0 + C++20 + CMake 的 Windows 桌面原型系统，主程序为 `PanTheraConsole.exe`。系统默认可以在仿真模式下运行；如果启用 MySQL，则需要额外配置 Qt MySQL 驱动、MySQL 客户端动态库和数据库连接信息。

重要限制：

- 当前系统是工程原型和仿真演示系统，不是已验证的医疗治疗系统。
- 默认设备链路使用 `SimulationDeviceFacade` 仿真设备，不直接控制真实治疗硬件。
- 真实患者数据、真实影像数据、账号密码和数据库备份在交付前必须做脱敏和权限控制。

## 2. 推荐交付方式

### 2.1 推荐方式 A：源码转移后在客户电脑重新构建

适合客户电脑需要继续开发、调试或二次集成。

需要转移的内容：

- `apps/`
- `src/`
- `tests/`
- `config/`
- `db/`
- `docs/`
- `scripts/`
- `cmake/`
- `CMakeLists.txt`
- `CMakePresets.json`
- `README.md`

不建议直接转移旧电脑上的 `build/` 目录。`build/` 中包含本机路径、编译器缓存、Qt 路径和中间文件，换电脑后容易出现路径失效。客户电脑应重新执行 CMake 配置和编译。

### 2.2 推荐方式 B：交付可运行程序包

适合客户只需要本地演示或试运行，不需要编译源码。

建议交付目录结构如下：

```text
PanTheraSys/
  PanTheraConsole/
    PanTheraConsole.exe
    Qt6Core.dll
    Qt6Gui.dll
    Qt6Widgets.dll
    Qt6Network.dll
    Qt6Sql.dll
    platforms/
      qwindows.dll
    imageformats/
    styles/
    sqldrivers/
      qsqlmysql.dll              # 仅启用 MySQL 时必须
    config/
      defaults.ini
      deployment.example.ini
    db/
      schema/
        mysql_5_7_init.sql
    runtime/
      images/
      reports/
```

注意：不要只复制 `PanTheraConsole.exe`。Qt 程序还依赖 Qt DLL、平台插件 `platforms/qwindows.dll`、可选 SQL 驱动、配置文件和数据库初始化 SQL。

## 3. 客户电脑基础环境

### 3.1 操作系统

- Windows 10 x64 或 Windows 11 x64
- 建议安装路径不要包含中文、特殊符号或过深目录，例如：`D:\PanTheraSys` 或 `C:\PanTheraSys`
- 如果放在 `C:\Program Files`，普通用户可能没有写入权限；建议将运行目录放在客户可写目录下

### 3.2 编译环境，仅源码构建需要

- Visual Studio 2019，安装 C++ 桌面开发工作负载
- MSVC x64 工具链
- CMake 3.21 或更高版本
- Ninja
- Qt 6.2.0 `msvc2019_64`

当前仓库的默认脚本假设 Qt 在：

```text
D:\Qt\6.2.0\msvc2019_64
```

如果客户电脑 Qt 安装路径不同，执行脚本时传入 `-QtRoot` 参数，或修改 `CMakePresets.json` 中的 `CMAKE_PREFIX_PATH`。

### 3.3 运行环境，仅程序包运行需要

Release 程序包建议客户安装：

- Microsoft Visual C++ Redistributable 2015-2022 x64
- Qt 运行时 DLL 和插件，通常由 `windeployqt` 复制到程序目录

不建议给客户交付 Debug 程序包。Debug 包会依赖 `Qt6Cored.dll`、`vcruntime140d.dll`、`msvcp140d.dll` 等调试运行库，客户电脑通常没有这些库，也不适合作为正式演示包分发。

## 4. 源码构建步骤

### 4.1 准备源码目录

将项目放到客户电脑，例如：

```powershell
cd D:\
```

最终目录示例：

```text
D:\PanTheraSys
```

### 4.2 配置 CMake

在 PowerShell 中进入项目根目录：

```powershell
cd D:\PanTheraSys
```

如果 Qt 路径是默认路径：

```powershell
.\scripts\configure-msvc2019.ps1
```

如果 Qt 路径不同：

```powershell
.\scripts\configure-msvc2019.ps1 -QtRoot "D:\Qt\6.2.0\msvc2019_64"
```

脚本会查找 Visual Studio、加载 x64 MSVC 环境，并使用 Qt 自带的 `qt-cmake.bat` 配置工程。

### 4.3 编译

```powershell
cmake --build build\msvc2019 --target PanTheraConsole
```

编译完成后，主程序通常位于：

```text
build\msvc2019\apps\console\PanTheraConsole.exe
```

构建后 `apps/console/CMakeLists.txt` 中的 post-build 步骤会尝试自动调用 `windeployqt`，把 Qt 运行时插件复制到可执行文件目录。

### 4.4 本机启动

推荐使用仓库脚本启动：

```powershell
.\scripts\run-console.ps1
```

如果 Qt 路径不同：

```powershell
.\scripts\run-console.ps1 -QtRoot "D:\Qt\6.2.0\msvc2019_64"
```

该脚本会设置：

- `PATH`
- `QT_PLUGIN_PATH`
- `QT_QPA_PLATFORM_PLUGIN_PATH`

这些变量用于避免 Qt 找不到 `qwindows.dll` 平台插件。

### 4.5 执行测试

```powershell
ctest --test-dir build\msvc2019 --output-on-failure
```

如果客户电脑没有 MySQL 或没有 QMYSQL 驱动，MySQL 集成测试会提示跳过或失败，需先完成第 6 节数据库配置。

## 5. Release 程序包制作建议

客户演示包建议使用 Release 构建。

在已加载 VS2019 x64 编译环境的命令行中执行：

```powershell
& "D:\Qt\6.2.0\msvc2019_64\bin\qt-cmake.bat" -S . -B build\msvc2019-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DPANTHERA_BUILD_TESTS=OFF
cmake --build build\msvc2019-release --target PanTheraConsole
```

如果构建后程序目录没有 Qt 插件，手动执行：

```powershell
& "D:\Qt\6.2.0\msvc2019_64\bin\windeployqt.exe" --release --no-translations "build\msvc2019-release\apps\console\PanTheraConsole.exe"
```

然后将以下内容复制到交付目录：

- `build\msvc2019-release\apps\console\PanTheraConsole.exe`
- 同目录下由 `windeployqt` 复制出的 Qt DLL 和插件目录
- `config\defaults.ini`
- `config\deployment.example.ini`
- `db\schema\mysql_5_7_init.sql`
- 空目录 `runtime\images`
- 空目录 `runtime\reports`

交付前建议删除：

- `*.pdb`
- `*.ilk`
- `CMakeFiles/`
- `cmake_install.cmake`
- 旧测试输出和本机临时文件

如果需要远程排查问题，可以单独保存 PDB 文件，不建议默认放入客户运行目录。

## 6. 配置文件说明

### 6.1 `config/defaults.ini`

程序启动时会查找：

```text
config/defaults.ini
```

查找位置包括当前工作目录、可执行文件目录以及部分上级目录。为了交付稳定，建议在程序包中把 `config/` 目录直接放在 `PanTheraConsole.exe` 同级。

默认配置示例：

```ini
[application]
organization=PanTheraSys
name=PanTheraSys Console
theme=midnight-clinical

[database]
enabled=false
driver=QMYSQL
host=127.0.0.1
port=3306
schema=panthera_sys
username=root
password=123456
connect_timeout_seconds=2

[storage]
image_root=runtime/images
report_root=runtime/reports

[display]
mode=three_screen
fullscreen=true
dashboard_screen=auto
planning_screen=auto
treatment_screen=auto
fallback_single_window=true

[simulation]
enabled=true
timer_interval_ms=1000
```

关键配置：

- `database/enabled=false`：使用内置种子数据，适合无数据库演示。
- `database/enabled=true`：启用 MySQL 持久化，需要完成 MySQL 配置。
- `storage/image_root`：影像文件目录，建议使用客户电脑可写目录。
- `storage/report_root`：报告文件目录，建议使用客户电脑可写目录。
- `display/mode=three_screen`：启用三屏医疗显示模式。
- `display/fullscreen=true`：三块屏幕全屏显示，隐藏普通窗口边框和任务栏干扰。
- `display/dashboard_screen`：设备监控屏。`auto` 时优先选择最左侧屏幕。
- `display/planning_screen`：治疗方案屏。`auto` 时优先选择剩余屏幕中位置靠上的屏幕。
- `display/treatment_screen`：治疗执行屏。`auto` 时优先选择最宽的主显示屏。
- `display/fallback_single_window=true`：客户电脑不足三块屏幕时自动回退到原单窗口模式。
- `simulation/enabled=true`：当前项目演示模式应保持启用。

交付前必须修改默认数据库密码，不要把 `root/123456` 作为客户现场配置。

### 6.2 三屏显示配置

客户现场三块屏幕建议按照片中的物理布局使用：

- 左侧竖屏：设备监控屏，对应图片 1。
- 上方横屏：治疗方案屏，对应图片 2。
- 下方主宽屏：治疗执行屏，对应图片 3。

程序默认会按屏幕几何位置自动分配。如果 Windows 的显示器编号或排列不符合现场，可以先运行：

```powershell
.\PanTheraConsole.exe --list-screens
```

根据输出的屏幕编号修改 `config/defaults.ini`：

```ini
[display]
mode=three_screen
fullscreen=true
dashboard_screen=0
planning_screen=1
treatment_screen=2
fallback_single_window=true
```

如果现场临时只有一块屏幕，可改为：

```ini
[display]
mode=single
```

### 6.3 `config/deployment.example.ini`

该文件用于记录部署环境、硬件模式和未来真实设备接入参数。当前项目主程序主要使用仿真设备，建议客户现场保持：

```ini
[deployment]
environment=customer-demo
device_mode=simulation

[hardware.motion]
adapter=simulation

[hardware.ultrasound]
adapter=simulation

[hardware.power]
adapter=simulation

[hardware.water_loop]
adapter=simulation
```

如果要接入真实硬件，不能只改配置文件。需要实现和验证真实设备适配器，并完成安全联锁、异常恢复和系统级测试。

## 7. MySQL 配置

### 7.1 不启用 MySQL 的演示方式

如果客户只需要打开软件看界面和流程：

```ini
[database]
enabled=false
```

此时系统会使用内置种子数据，不要求客户安装 MySQL，也不要求 QMYSQL 驱动。

### 7.2 启用 MySQL 的前置条件

如果客户需要数据持久化：

- 安装 MySQL 5.7.x 或兼容版本
- 创建 `panthera_sys` 数据库
- 配置数据库账号和密码
- Qt 运行目录必须有 QMYSQL 驱动
- MySQL 客户端 DLL 必须能被程序加载

位数和编译器必须一致：

- Windows x64
- Qt `msvc2019_64`
- MySQL client x64
- QMYSQL plugin x64

### 7.3 初始化数据库

使用 MySQL 管理账号执行：

```powershell
mysql -u root -p < db\schema\mysql_5_7_init.sql
```

建议创建应用专用账号，不要让程序使用 root：

```sql
CREATE USER 'panthera_app'@'localhost' IDENTIFIED BY '请替换为强密码';
GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES ON panthera_sys.* TO 'panthera_app'@'localhost';
FLUSH PRIVILEGES;
```

然后修改 `config/defaults.ini`：

```ini
[database]
enabled=true
driver=QMYSQL
host=127.0.0.1
port=3306
schema=panthera_sys
username=panthera_app
password=请替换为强密码
connect_timeout_seconds=2
```

注意：程序会在连接成功后尝试执行 `db/schema/mysql_5_7_init.sql` 做表结构初始化。如果数据库本身不存在，程序在打开连接时就会失败，所以首次启用前应先手动创建数据库或执行初始化 SQL。

### 7.4 Qt MySQL 驱动

启用 MySQL 时，程序目录需要存在：

```text
sqldrivers/qsqlmysql.dll
```

如果是 Debug 构建，对应文件通常是：

```text
sqldrivers/qsqlmysqld.dll
```

还需要 MySQL 客户端 DLL，例如：

```text
libmysql.dll
```

`libmysql.dll` 可以放在 `PanTheraConsole.exe` 同级目录，也可以放在系统 `PATH` 中。

常见问题：

- 只有 `qsqlmysql.dll` 但没有 `libmysql.dll`，驱动仍会加载失败。
- Qt、MySQL client、程序三者位数不一致，驱动会加载失败。
- Debug 程序不能使用 Release 版 `qsqlmysql.dll`，Release 程序也不能使用 Debug 版 `qsqlmysqld.dll`。

## 8. 客户电脑启动检查清单

### 8.1 文件检查

确认程序目录包含：

- `PanTheraConsole.exe`
- `Qt6Core.dll`
- `Qt6Gui.dll`
- `Qt6Widgets.dll`
- `Qt6Network.dll`
- `Qt6Sql.dll`
- `platforms/qwindows.dll`
- `config/defaults.ini`
- `db/schema/mysql_5_7_init.sql`
- `runtime/images`
- `runtime/reports`

如果启用 MySQL，还要确认：

- `sqldrivers/qsqlmysql.dll`
- `libmysql.dll`
- MySQL 服务已启动
- `defaults.ini` 中数据库地址、端口、库名、用户名、密码正确

### 8.2 首次启动验证

1. 双击或从 PowerShell 启动 `PanTheraConsole.exe`。
2. 确认主窗口能打开。
3. 确认设备监控、治疗方案、治疗、数据管理等主要页面能切换。
4. 如果 `database/enabled=false`，确认能看到种子演示数据。
5. 如果 `database/enabled=true`，新增或编辑一条患者记录，关闭程序后重新打开，确认数据仍存在。
6. 检查 `runtime/images` 和 `runtime/reports` 是否可写。

### 8.3 源码环境验证

客户需要开发或调试时，建议执行：

```powershell
.\scripts\validate-utf8.ps1
ctest --test-dir build\msvc2019 --output-on-failure
```

如果测试不通过，先确认 Qt 路径、MySQL 驱动、数据库账号和运行时 PATH。

## 9. 常见问题与处理

| 现象 | 可能原因 | 处理方式 |
| --- | --- | --- |
| CMake 找不到 Qt6 | Qt 未安装或路径不一致 | 执行脚本时传入 `-QtRoot`，或修改 `CMAKE_PREFIX_PATH` |
| 找不到 MSVC 编译器 | 未安装 VS2019 C++ 工具 | 安装 Visual Studio 2019 的 C++ 桌面开发工作负载 |
| 启动提示找不到 Qt platform plugin windows | 缺少 `platforms/qwindows.dll` 或插件路径错误 | 使用 `windeployqt` 重新部署，或使用 `scripts/run-console.ps1` 启动 |
| 启动提示缺少 VCRUNTIME/MSVCP DLL | 未安装 VC++ 运行库或交付了 Debug 包 | Release 包安装 VC++ Redistributable；不要交付 Debug 包 |
| MySQL 连接失败 | 数据库未启动、库不存在、账号密码错误、防火墙拦截 | 检查 MySQL 服务、执行初始化 SQL、核对 `defaults.ini` |
| 提示 QMYSQL driver unavailable | 缺少 Qt MySQL 驱动 | 将匹配 Qt 6.2.0 msvc2019_64 的 `qsqlmysql.dll` 放入 `sqldrivers/` |
| QMYSQL 文件存在但仍加载失败 | 缺少 `libmysql.dll` 或位数不匹配 | 将 x64 `libmysql.dll` 放到 exe 同级目录，确认版本匹配 |
| 中文显示或日志乱码 | 控制台编码或文件编码异常 | 使用 UTF-8 文件；构建参数已使用 `/utf-8`；PowerShell 可执行 `chcp 65001` |
| 双击可执行文件后找不到配置 | `config/` 未放在 exe 同级目录 | 将 `config/` 复制到 `PanTheraConsole.exe` 同级 |
| 客户目录下无法写入报告或图片 | 安装目录权限不足 | 将程序放到可写目录，或把 `storage/*_root` 改为用户可写路径 |

## 10. 交付前注意事项

- 使用 Release 包交付客户，不使用 Debug 包。
- 不要复制旧机器的 `build/` 缓存作为客户源码构建基础。
- 不要把真实患者资料、真实检查影像或内部测试数据直接放入交付包。
- 修改数据库默认密码，并使用应用专用账号。
- 确认客户现场数据库、Qt 插件、MySQL 客户端 DLL 都是 x64。
- 确认程序目录存在 `config/`、`db/`、`runtime/`。
- 保持 `database/enabled=false` 可以降低演示环境复杂度；需要数据持久化时再启用 MySQL。
- 当前项目硬件为仿真模式，不能通过改配置直接变成真实治疗设备软件。
- 如果客户需要后续真实设备接入，应另行制定硬件接口、风险控制、测试验证和验收方案。

## 11. 建议验收记录

交付时建议记录以下信息：

| 项目 | 记录 |
| --- | --- |
| 客户电脑系统版本 | Windows 10/11 x64 |
| 安装路径 | 例如 `D:\PanTheraSys` |
| 程序版本或提交号 | 填写 Git commit 或交付版本号 |
| 构建类型 | Release 或 Debug |
| Qt 版本 | Qt 6.2.0 msvc2019_64 |
| 是否启用 MySQL | 是/否 |
| MySQL 版本 | 例如 MySQL 5.7.x |
| 数据库名 | `panthera_sys` |
| 启动验证 | 通过/未通过 |
| 页面切换验证 | 通过/未通过 |
| 数据持久化验证 | 通过/未通过或不适用 |
| 备注 | 记录现场问题 |

## 12. 最小可运行方案

如果只需要让客户尽快打开项目演示，最小方案是：

1. 使用 Release 构建 `PanTheraConsole.exe`。
2. 执行 `windeployqt` 生成运行时目录。
3. 将 `config/defaults.ini` 放到 exe 同级的 `config/` 目录。
4. 保持 `database/enabled=false`。
5. 将 `db/schema/mysql_5_7_init.sql` 放到 exe 同级的 `db/schema/` 目录。
6. 创建 `runtime/images` 和 `runtime/reports`。
7. 客户电脑安装 VC++ Redistributable x64。
8. 双击 `PanTheraConsole.exe` 验证主窗口和主要页面。
