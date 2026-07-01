# DOBOT Trajectory Sync for PanTheraSys

这是给 Codex 集成用的 Qt/C++ 开发包，用于实现：

- 从 DOBOT 控制器 SFTP 目录读取轨迹复现 `.csv` 文件；
- 刷新到 PanTheraSys 的“预设轨迹”下拉框；
- 通过 DOBOT TCP/IP `StartPath` 指令运行轨迹。

请先读：

```text
docs/README_开发说明.md
scripts/codex_task_prompt.md
```

核心目录：

```text
/dobot/userdata/project/process/trajectory/
```

核心指令：

```text
StartPath(文件名.csv,isConst=0,multi=1.00,sample=50,freq=0.200)
```
