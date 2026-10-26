# -*- coding: utf-8 -*-
from __future__ import annotations

import configparser
import re
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Iterable, Sequence

from docx import Document
from docx.enum.section import WD_ORIENT
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_ROW_HEIGHT_RULE, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK, WD_LINE_SPACING
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Inches, Pt, RGBColor


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "docs" / "handover"
OUT_PATH = OUT_DIR / "PanTheraSys项目全面交接说明书_2026-10-14.docx"
SNAPSHOT_DATE = "2026-10-14"


def git(*args: str) -> str:
    try:
        result = subprocess.run(
            ["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False
        )
        return result.stdout.strip()
    except OSError:
        return ""


def set_cell_shading(cell, fill: str) -> None:
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = tc_pr.find(qn("w:shd"))
    if shd is None:
        shd = OxmlElement("w:shd")
        tc_pr.append(shd)
    shd.set(qn("w:fill"), fill)


def set_cell_margins(cell, top=80, start=90, bottom=80, end=90) -> None:
    tc = cell._tc
    tc_pr = tc.get_or_add_tcPr()
    tc_mar = tc_pr.first_child_found_in("w:tcMar")
    if tc_mar is None:
        tc_mar = OxmlElement("w:tcMar")
        tc_pr.append(tc_mar)
    for m, value in (("top", top), ("start", start), ("bottom", bottom), ("end", end)):
        node = tc_mar.find(qn(f"w:{m}"))
        if node is None:
            node = OxmlElement(f"w:{m}")
            tc_mar.append(node)
        node.set(qn("w:w"), str(value))
        node.set(qn("w:type"), "dxa")


def set_repeat_table_header(row) -> None:
    tr_pr = row._tr.get_or_add_trPr()
    tbl_header = OxmlElement("w:tblHeader")
    tbl_header.set(qn("w:val"), "true")
    tr_pr.append(tbl_header)


def keep_row_together(row) -> None:
    tr_pr = row._tr.get_or_add_trPr()
    cant_split = OxmlElement("w:cantSplit")
    tr_pr.append(cant_split)


def set_run_font(run, east_asia="宋体", ascii_font="Calibri", size=10.5, bold=None, color=None) -> None:
    run.font.name = ascii_font
    run._element.rPr.rFonts.set(qn("w:eastAsia"), east_asia)
    run.font.size = Pt(size)
    if bold is not None:
        run.bold = bold
    if color is not None:
        run.font.color.rgb = RGBColor.from_string(color)


def field_run(paragraph, instruction: str) -> None:
    run = paragraph.add_run()
    begin = OxmlElement("w:fldChar")
    begin.set(qn("w:fldCharType"), "begin")
    instr = OxmlElement("w:instrText")
    instr.set(qn("xml:space"), "preserve")
    instr.text = instruction
    separate = OxmlElement("w:fldChar")
    separate.set(qn("w:fldCharType"), "separate")
    text = OxmlElement("w:t")
    text.text = "更新域后显示"
    end = OxmlElement("w:fldChar")
    end.set(qn("w:fldCharType"), "end")
    run._r.extend([begin, instr, separate, text, end])


def configure_document(doc: Document) -> None:
    section = doc.sections[0]
    section.top_margin = Cm(2.0)
    section.bottom_margin = Cm(1.8)
    section.left_margin = Cm(2.1)
    section.right_margin = Cm(1.8)
    section.header_distance = Cm(0.8)
    section.footer_distance = Cm(0.7)

    styles = doc.styles
    normal = styles["Normal"]
    normal.font.name = "Calibri"
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), "宋体")
    normal.font.size = Pt(10.5)
    normal.paragraph_format.line_spacing_rule = WD_LINE_SPACING.ONE_POINT_FIVE
    normal.paragraph_format.space_after = Pt(4)

    heading_sizes = {1: 18, 2: 15, 3: 13, 4: 11.5}
    for level, size in heading_sizes.items():
        style = styles[f"Heading {level}"]
        style.font.name = "Arial"
        style._element.rPr.rFonts.set(qn("w:eastAsia"), "微软雅黑")
        style.font.size = Pt(size)
        style.font.bold = True
        style.font.color.rgb = RGBColor(24, 63, 92)
        style.paragraph_format.space_before = Pt(12 if level <= 2 else 8)
        style.paragraph_format.space_after = Pt(5)
        style.paragraph_format.keep_with_next = True

    for section in doc.sections:
        header = section.header.paragraphs[0]
        header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        r = header.add_run("PanTheraSys 项目全面交接说明书  |  工作区快照 2026-10-14")
        set_run_font(r, east_asia="微软雅黑", size=8.5, color="6B7785")
        footer = section.footer.paragraphs[0]
        footer.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = footer.add_run("内部交接资料  ·  第 ")
        set_run_font(r, size=8.5, color="6B7785")
        field_run(footer, " PAGE ")
        r = footer.add_run(" 页 / 共 ")
        set_run_font(r, size=8.5, color="6B7785")
        field_run(footer, " NUMPAGES ")
        r = footer.add_run(" 页")
        set_run_font(r, size=8.5, color="6B7785")

    settings = doc.settings._element
    update_fields = settings.find(qn("w:updateFields"))
    if update_fields is None:
        update_fields = OxmlElement("w:updateFields")
        settings.append(update_fields)
    update_fields.set(qn("w:val"), "true")


def add_heading(doc: Document, text: str, level: int = 1) -> None:
    doc.add_heading(text, level=level)


def add_paragraph(doc: Document, text: str = "", bold_prefix: str | None = None) -> None:
    p = doc.add_paragraph()
    p.paragraph_format.widow_control = True
    if bold_prefix and text.startswith(bold_prefix):
        r = p.add_run(bold_prefix)
        set_run_font(r, bold=True)
        r = p.add_run(text[len(bold_prefix):])
        set_run_font(r)
    else:
        r = p.add_run(text)
        set_run_font(r)


def add_bullets(doc: Document, items: Iterable[str], level: int = 0) -> None:
    for item in items:
        p = doc.add_paragraph(style="List Bullet" if level == 0 else "List Bullet 2")
        p.paragraph_format.left_indent = Cm(0.5 + 0.45 * level)
        p.paragraph_format.first_line_indent = Cm(-0.25)
        p.paragraph_format.space_after = Pt(2)
        r = p.add_run(str(item))
        set_run_font(r)


def add_numbered(doc: Document, items: Iterable[str]) -> None:
    for item in items:
        p = doc.add_paragraph(style="List Number")
        p.paragraph_format.left_indent = Cm(0.5)
        p.paragraph_format.first_line_indent = Cm(-0.25)
        r = p.add_run(str(item))
        set_run_font(r)


def add_code(doc: Document, text: str) -> None:
    table = doc.add_table(rows=1, cols=1)
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.style = "Table Grid"
    cell = table.cell(0, 0)
    set_cell_shading(cell, "F4F6F8")
    set_cell_margins(cell, 120, 140, 120, 140)
    p = cell.paragraphs[0]
    p.paragraph_format.line_spacing = 1.0
    for index, line in enumerate(text.splitlines() or [""]):
        if index:
            p.add_run().add_break()
        r = p.add_run(line)
        set_run_font(r, east_asia="等线", ascii_font="Consolas", size=8.5)


def add_callout(doc: Document, title: str, text: str, fill="FFF2CC", border_color="D6B656") -> None:
    table = doc.add_table(rows=1, cols=1)
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.style = "Table Grid"
    cell = table.cell(0, 0)
    set_cell_shading(cell, fill)
    set_cell_margins(cell, 130, 150, 130, 150)
    p = cell.paragraphs[0]
    r = p.add_run(f"{title}：")
    set_run_font(r, east_asia="微软雅黑", bold=True, size=10.5, color="7A4E00")
    r = p.add_run(text)
    set_run_font(r, size=10.5)
    doc.add_paragraph().paragraph_format.space_after = Pt(0)


def add_table(doc: Document, headers: Sequence[str], rows: Sequence[Sequence[object]], font_size=8.5, widths=None):
    table = doc.add_table(rows=1, cols=len(headers))
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.autofit = True
    hdr = table.rows[0]
    set_repeat_table_header(hdr)
    for index, header in enumerate(headers):
        cell = hdr.cells[index]
        set_cell_shading(cell, "1F4E78")
        set_cell_margins(cell)
        cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = p.add_run(str(header))
        set_run_font(r, east_asia="微软雅黑", size=font_size, bold=True, color="FFFFFF")
    for row_index, values in enumerate(rows):
        row = table.add_row()
        keep_row_together(row)
        if row_index % 2:
            for cell in row.cells:
                set_cell_shading(cell, "F4F8FB")
        for index, value in enumerate(values):
            cell = row.cells[index]
            set_cell_margins(cell)
            cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.TOP
            p = cell.paragraphs[0]
            p.paragraph_format.line_spacing = 1.05
            p.paragraph_format.space_after = Pt(0)
            r = p.add_run("" if value is None else str(value))
            set_run_font(r, size=font_size)
    if widths:
        for row in table.rows:
            for idx, width_cm in enumerate(widths):
                if idx < len(row.cells):
                    row.cells[idx].width = Cm(width_cm)
    doc.add_paragraph().paragraph_format.space_after = Pt(0)
    return table


def add_page_break(doc: Document) -> None:
    doc.add_page_break()


def add_cover(doc: Document, revision: str, commit: str) -> None:
    p = doc.add_paragraph()
    p.paragraph_format.space_before = Pt(80)
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run("PanTheraSys")
    set_run_font(r, east_asia="微软雅黑", ascii_font="Arial", size=30, bold=True, color="163E5C")
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run("项目全面交接说明书")
    set_run_font(r, east_asia="微软雅黑", size=25, bold=True, color="1F4E78")
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run("软件架构 · 三屏功能 · 硬件接口 · 数据模型 · 部署验证 · 风险清单")
    set_run_font(r, east_asia="微软雅黑", size=12, color="52697A")
    doc.add_paragraph()
    add_table(
        doc,
        ["项目项", "当前基线"],
        [
            ["文档版本", revision],
            ["分析对象", str(ROOT)],
            ["快照日期", SNAPSHOT_DATE],
            ["Git 分支", git("branch", "--show-current") or "无法读取"],
            ["最近提交", commit or "无法读取"],
            ["产品声明版本", "CMake 0.1.0；界面 V1.0"],
            ["文档性质", "内部研发交接；按当前本地工作区实物编制"],
        ],
        font_size=9.5,
        widths=[4.0, 12.0],
    )
    add_callout(
        doc,
        "重要声明",
        "该仓库 README 将产品定义为非临床原型。本文档描述“代码中存在的能力”，不构成医疗器械验证、临床安全认证或允许对人体实施治疗的证明。所有真实硬件治疗必须在完成系统级风险控制、实机联调、验证确认和法规流程后才可启用。",
        fill="FCE4D6",
    )
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(36)
    r = p.add_run("编制：项目第一负责人/开发负责人视角的源码事实核查\n生成日期：2026-10-14（Asia/Shanghai）")
    set_run_font(r, east_asia="微软雅黑", size=10.5, color="52697A")
    add_page_break(doc)


def parse_schema(sql_text: str):
    tables = []
    pattern = re.compile(r"CREATE TABLE IF NOT EXISTS\s+([A-Za-z0-9_]+)\s*\((.*?)\)\s*ENGINE=InnoDB", re.S | re.I)
    for match in pattern.finditer(sql_text):
        name = match.group(1)
        body = match.group(2)
        entries = []
        current = ""
        depth = 0
        for ch in body:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            if ch == "," and depth == 0:
                entries.append(current.strip())
                current = ""
            else:
                current += ch
        if current.strip():
            entries.append(current.strip())
        columns, constraints = [], []
        for entry in entries:
            flat = " ".join(entry.split())
            upper = flat.upper()
            if upper.startswith(("CONSTRAINT ", "INDEX ", "KEY ", "UNIQUE KEY ", "PRIMARY KEY ", "FOREIGN KEY ")):
                constraints.append(flat)
            else:
                parts = flat.split(None, 1)
                if len(parts) == 2:
                    columns.append((parts[0].strip("`"), parts[1]))
                else:
                    constraints.append(flat)
        tables.append((name, columns, constraints))
    return tables


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    commit_hash = git("rev-parse", "HEAD")
    commit_date = git("log", "-1", "--date=iso-strict", "--pretty=format:%ad")
    commit_subject = git("log", "-1", "--pretty=format:%s")
    commit_summary = f"{commit_hash[:12]} / {commit_date} / {commit_subject}" if commit_hash else "无法读取"
    status_lines = git("status", "--short").splitlines()

    doc = Document()
    configure_document(doc)
    doc.core_properties.title = "PanTheraSys 项目全面交接说明书"
    doc.core_properties.subject = "PanTheraSys 软件、硬件、三屏页面、接口参数与交接风险"
    doc.core_properties.author = "OpenAI Codex（依据本地源码自动汇编）"
    doc.core_properties.keywords = "PanTheraSys, 交接, Qt, C++, MySQL, Dobot, UIM, 超声"
    doc.core_properties.comments = "基于 2026-10-14 本地工作区快照，包含未提交修改。"

    add_cover(doc, "V1.0-HANDOVER", commit_summary)

    add_heading(doc, "文档控制", 1)
    add_table(
        doc,
        ["项", "说明"],
        [
            ["阅读对象", "对本项目完全不了解的接手开发、测试、硬件、实施与质量团队"],
            ["事实优先级", "2026-10-14 本地源码 > 当前配置文件 > 数据库建表脚本 > 历史文档/README"],
            ["功能状态术语", "“可见已实现”=当前 UI 有入口；“代码已实现未接入”=有函数/适配器但当前 UI 不可达；“模拟”=数据或执行来自 SimulationDeviceFacade；“待实机验证”=协议代码存在但本次未连接设备"],
            ["范围", "apps、src、tests、config、db、docs、scripts、CMake、运行时目录；排除 build 生成物的逐文件审计和 _codex 外部厂商示例的产品功能认定"],
            ["敏感信息", "本文按用户要求保留当前接口参数与默认口令，用于复现；只允许内部受控流转"],
        ],
        font_size=9,
        widths=[3.2, 12.8],
    )
    add_heading(doc, "版本记录", 2)
    add_table(doc, ["版本", "日期", "编制依据", "变更摘要"], [["V1.0-HANDOVER", SNAPSHOT_DATE, "本地工作区全面静态扫描 + 主目标重编译 + CTest 运行", "首次完整交接版"]])
    add_heading(doc, "目录", 1)
    p = doc.add_paragraph()
    field_run(p, ' TOC \\o "1-4" \\h \\z \\u ')
    add_paragraph(doc, "提示：首次用 Microsoft Word 打开后，按 Ctrl+A、F9 更新目录、页码和字段。")
    add_page_break(doc)

    add_heading(doc, "1. 执行摘要与接手结论", 1)
    add_paragraph(doc, "PanTheraSys 是一套 Windows x64 上的 Qt Widgets/C++20 低强度超声治疗工作站原型。当前程序将临床流程拆为设备监控、治疗计划、治疗执行三个物理屏幕；屏幕不足时回退到单窗口，并额外提供数据管理工作区。系统已经具有患者/影像/计划/治疗/报告数据链、医生登录、部分真实硬件驱动、轨迹与三轴电机控制、影像标注和治疗证据归档，但关键治疗闭环仍同时混有真实硬件控制和仿真状态。")
    add_heading(doc, "1.1 当前可交接的核心资产", 2)
    add_bullets(doc, [
        "可编译的 PanTheraConsole 主程序，当前工作区主目标于 2026-10-14 重新编译成功。",
        "三屏临床工作流：设备监控屏、治疗计划屏、治疗执行屏；少于三屏时可单窗口回退。",
        "MySQL 5.7 兼容临床数据与证据模型，共 29 张表；登录口令使用 PBKDF2-HMAC-SHA256。",
        "真实硬件适配代码：Dobot TCP/SFTP、UIM USB-CAN 三轴、Modbus 水泵、LU-926 温控、液位传感器、Aigtek 功放、RIGOL VISA、DirectShow USB 摄像头。",
        "影像采集/分层、标注、当前与历史对比、三维重建、点/线靶点生成、呼吸偏移补偿、计划审批与治疗结果留证。",
        "安全内核、审计服务、应用上下文、仓储抽象、仿真设备门面以及 13 个自动化测试目标。",
    ])
    add_heading(doc, "1.2 必须先接受的现状边界", 2)
    add_table(
        doc,
        ["级别", "结论", "交接含义"],
        [
            ["P0", "不是可直接用于人体治疗的已验证医疗器械软件", "所有真实治疗输出必须保持禁用，先完成危害分析、双通道安全、校准、系统验证与法规流程"],
            ["P0", "治疗页的安全联锁健康值主要由 SimulationDeviceFacade 上报", "设备监控页虽可连接真硬件，但真硬件状态没有完整汇聚进 SafetyKernel"],
            ["P0", "治疗执行时的功率开关调用仿真门面", "界面“治疗进行中”不等于 RIGOL/Aigtek 真实声学输出闭环已经执行"],
            ["P1", "当前启动强制依赖 MySQL 与医生登录", "即使配置 database/enabled=false，程序仍会弹错并退出；旧文档关于 seed 离线启动已失效"],
            ["P1", "本地工作区不是干净提交", f"当前有 {len(status_lines)} 条修改/未跟踪状态；交接前必须形成正式 commit、tag 与变更说明"],
            ["P1", "自动化测试不是全绿", "已有测试二进制运行 11/13 通过；影像算法失败、计划页回归超时；当前全量测试重新链接另有无诊断失败"],
            ["P1", "配置内存在明文弱口令", "root/123456、root/dobot 必须轮换并迁出仓库"],
        ],
        font_size=8.5,
        widths=[1.4, 6.2, 8.4],
    )
    add_callout(doc, "接手第一原则", "先冻结当前工作区、形成可追溯版本，再做任何功能修改。否则当前未提交的登录、证据、数据库和三屏功能可能无法复现。", fill="FCE4D6")

    add_heading(doc, "2. 审计快照、仓库范围与版本状态", 1)
    add_heading(doc, "2.1 扫描范围", 2)
    add_table(
        doc,
        ["区域", "职责", "本次核查重点"],
        [
            ["apps/console", "进程入口与三屏窗口编排", "启动、数据库/登录、摄像头、屏幕映射、退出"],
            ["src/core", "领域模型、上下文、事件、安全、服务", "联锁条件、状态机、跨屏共享状态、审计"],
            ["src/adapters", "数据库、硬件与仿真适配", "协议命令、串口/TCP 参数、数据持久化"],
            ["src/modules", "登录、设备监控、计划、治疗、数据管理 UI", "页面入口、按钮行为、参数范围、不可达功能"],
            ["db/schema", "MySQL 初始化", "29 张表、外键、索引、证据链"],
            ["config", "运行参数", "显示、数据库、存储、Dobot、安全限速、口令"],
            ["tests", "自动化测试", "13 个测试目标与本机结果"],
            ["docs/scripts", "历史说明与构建脚本", "与当前源码的差异、部署依赖"],
            ["build", "本机生成物", "只用于识别实际 Qt/编译器和验证，不作为源码基线"],
        ],
    )
    add_paragraph(doc, "扫描统计：排除 build 与 .git 后共识别约 180 个文件；其中 .cpp 54 个、.h 46 个、.md 20 个；选定文本/代码约 42,984 行。统计包含 _codex 厂商参考资料，因此产品代码规模应以 apps/src/tests 为准。")
    add_heading(doc, "2.2 Git 基线与本地改动", 2)
    add_table(doc, ["项", "值"], [["分支", git("branch", "--show-current")], ["HEAD", commit_hash], ["提交时间", commit_date], ["提交说明", commit_subject], ["工作区", f"非干净；{len(status_lines)} 条状态记录"]], font_size=9)
    add_paragraph(doc, "以下为生成本文档时的 git status --short：")
    add_code(doc, "\n".join(status_lines) if status_lines else "（工作区干净）")
    add_callout(doc, "风险", "最近提交的说明为“7.10，只存在基础表，其余数据表新建前”，而当前本地代码已经扩展大量表和工作流。这证明最新功能主要在未提交工作区中，不能只移交 Git HEAD。", fill="FFF2CC")

    add_heading(doc, "3. 系统架构", 1)
    add_heading(doc, "3.1 分层与编译单元", 2)
    add_table(
        doc,
        ["层", "CMake 目标/目录", "职责", "依赖方向"],
        [
            ["进程/编排层", "PanTheraConsole / apps/console", "初始化服务、数据库、登录、摄像头、三屏/单屏窗口", "依赖 modules"],
            ["界面模块层", "panthera_modules / src/modules", "auth、dashboard、planning、treatment、data、shell、shared", "依赖 core + adapters + Qt Widgets"],
            ["适配层", "panthera_adapters / src/adapters", "MySQL、仿真、串口、TCP/SFTP、VISA、UIM SDK", "依赖 core；不应包含临床页面策略"],
            ["核心层", "panthera_core / src/core", "领域类型、ApplicationContext、EventBus、SafetyKernel、审计、临床数据服务", "只依赖 Qt Core/Gui"],
            ["基础设施", "MySQL/文件系统/设备/外部 DLL", "持久化与物理 I/O", "被 adapters 访问"],
        ],
        font_size=8.7,
    )
    add_code(doc, """┌──────────────────────────────────────────────────────────────┐
│ PanTheraConsole：启动、登录、三屏窗口、摄像头守护              │
└───────────────┬──────────────────────────────────────────────┘
                ▼
┌──────────────────────────────────────────────────────────────┐
│ UI modules：Dashboard / Planning / Treatment / Data / Auth    │
└───────────────┬───────────────────────┬──────────────────────┘
                ▼                       ▼
┌────────────────────────────┐  ┌──────────────────────────────┐
│ core：Context/Event/Safety  │  │ adapters：MySQL/Sim/Hardware│
└───────────────┬────────────┘  └──────────────┬───────────────┘
                └───────────────────────────────▼
          MySQL 5.7 / runtime 文件 / TCP / SFTP / RS485 / VISA / CAN""")
    add_heading(doc, "3.2 运行时核心对象", 2)
    add_table(
        doc,
        ["对象", "作用", "关键事实"],
        [
            ["ApplicationContext", "共享登录医生、患者、活动计划、治疗摄像头帧、运行模式", "三屏窗口持有同一个实例，因此患者/计划选择能跨屏同步"],
            ["EventBus", "Qt signal/slot 事件中心", "用于跨模块广播领域变化"],
            ["SafetyKernel", "统一计算开始/暂停/继续/中止条件", "红色硬件联锁在 Treating/Paused 状态会转 Alarm 并发出 treatmentAbortRequested"],
            ["AuditService", "收集通用审计项", "部分新证据同时进入 MySQL workflow repository；仍有 actor=system 的路径"],
            ["ClinicalDataService", "对仓储做验证与 CRUD 编排", "启动时可将 seed 数据引导入空 MySQL，但当前无 MySQL仍退出"],
            ["SimulationDeviceFacade", "每秒生成设备快照、健康信号、仿真功率输出", "当前始终 start；simulation/enabled 配置没有真正控制其创建/启动"],
            ["DobotControllerClient", "Dobot dashboard/motion 双 TCP 客户端", "同一实例注入三屏页面，主要由设备监控与计划页使用"],
            ["UsbCameraFrameSource", "Windows DirectShow 摄像头采集", "优先 USB3 PLUS Video；帧写入 ApplicationContext"],
        ],
    )
    add_heading(doc, "3.3 临床数据与控制数据流", 2)
    add_numbered(doc, [
        "医生登录成功，AuthenticatedOperator 写入 ApplicationContext；随后才创建三屏窗口。",
        "数据管理或计划屏选择患者，Context 发出 selectedPatientChanged；SafetyKernel 将“已选患者”置真。",
        "计划屏从 USB 摄像头共享帧采集分层影像，标注并生成靶点，保存草稿/批准计划到 MySQL。",
        "批准计划成为 activePlan 后，SafetyKernel 接收 approvalState；若仿真健康项均通过，则进入 TreatmentReady。",
        "治疗屏按层读取 TherapySegment/Points，驱动 UIM 7 号层轴、8 号垂直轴、6 号摆动轴；功率开关与健康状态当前仍走仿真设备。",
        "结束/继续治疗时写 treatment_session、treatment_record、影像标注快照、设备快照、终止日志；完全终止时额外生成报告和 DOCX 证据。",
    ])
    add_heading(doc, "3.4 安全状态机与联锁", 2)
    add_table(
        doc,
        ["条件", "对应 InterlockReason", "来源（当前）", "状态影响"],
        [
            ["已选择患者", "NoPatientSelected", "ApplicationContext", "缺失时黄色"],
            ["计划 Approved/Locked", "PlanNotApproved", "ApplicationContext.activePlan", "缺失时黄色"],
            ["水循环健康", "WaterLoopFault", "SimulationDeviceFacade", "失败时红色"],
            ["温度健康", "TemperatureFault", "SimulationDeviceFacade", "失败时红色"],
            ["功率就绪", "PowerFault", "SimulationDeviceFacade", "失败时红色"],
            ["运动就绪", "MotionFault", "SimulationDeviceFacade；三轴急停会注入异常", "失败时红色"],
            ["急停释放", "EmergencyStop", "SimulationDeviceFacade；三轴软件急停会注入", "失败时红色"],
            ["超声可用", "UltrasoundUnavailable", "SimulationDeviceFacade", "失败时红色"],
        ],
        font_size=8.2,
    )
    add_paragraph(doc, "状态序列：Startup → Idle/Planning → TreatmentReady → Treating ⇄ Paused → Idle；任一硬件类红色联锁可把 Treating/Paused 转为 Alarm。当前真实水泵、温控、液位、RIGOL、Aigtek、Dobot 状态没有形成完整的一致安全快照，这是最重要的架构缺口。")

    add_heading(doc, "4. 技术栈、版本与构建", 1)
    add_table(
        doc,
        ["类别", "当前事实", "来源/备注"],
        [
            ["产品版本", "project VERSION 0.1.0；界面标题“低强度超声系统 V1.0”", "CMakeLists.txt / main_window.cpp；版本源不统一"],
            ["语言标准", "C++20", "CMAKE_CXX_STANDARD 20"],
            ["GUI", "Qt 6.2.0，Widgets/Core/Gui", "本机 Qt6_DIR=D:/PanSoftware/QT/6.2.0/mingw81_64"],
            ["其他 Qt 模块", "Network、SerialPort、Sql、Test", "CMake find_package"],
            ["构建", "CMake ≥3.21；AUTOMOC/AUTORCC/AUTOUIC", "禁止源码内构建"],
            ["本机编译器", "MinGW 8.1 64-bit；GNU 警告 -Wall -Wextra -Wpedantic", "CMakeCache/preset"],
            ["另一路径", "README/脚本仍描述 VS2019 + Qt 6.2.0 msvc2019_64", "应选择并冻结唯一正式工具链"],
            ["包管理", "vcpkg x64-mingw-dynamic", "OpenSSL、ZLIB、libssh2"],
            ["数据库", "MySQL 5.7 兼容；Qt QMYSQL", "schema/mysql_5_7_init.sql"],
            ["平台", "Windows x64", "DirectShow、Win32 DLL、串口、windeployqt"],
            ["UIM SDK", "UISimCanFunc.dll（外部二进制）", "从三个硬编码候选路径择一并复制到 exe 目录"],
        ],
    )
    add_heading(doc, "4.1 当前推荐的本机 MinGW 构建", 2)
    add_code(doc, """cmake --preset mingw-vcpkg-debug
cmake --build --preset mingw-vcpkg-debug

# 注意：该 preset 将 PANTHERA_BUILD_TESTS=OFF，不能据此运行测试。
# 需要测试时另建启用 PANTHERA_BUILD_TESTS=ON 的构建目录。""")
    add_heading(doc, "4.2 生成目标与部署动作", 2)
    add_bullets(doc, [
        "静态库 panthera_core、panthera_adapters、panthera_modules；可执行文件 PanTheraConsole.exe。",
        "Windows post-build 尝试复制 UISimCanFunc.dll。",
        "若找到 windeployqt，则复制 Qt DLL、platforms、imageformats、styles、sqldrivers、TLS 插件。",
        "本次重编译主目标成功，但 vcpkg applocal.ps1 报告找不到 dumpbin/llvm-objdump/objdump，不能解析附加 DLL 依赖；交付包必须另做依赖检查。",
    ])

    add_heading(doc, "5. 启动、登录、显示与运行目录", 1)
    add_heading(doc, "5.1 实际启动顺序", 2)
    add_numbered(doc, [
        "预配置 Qt platform plugin 搜索路径；创建 QApplication；--list-screens 则打印显示器并退出。",
        "注册领域 Qt 元类型并加载 :/styles/panthera.qss。",
        "创建 EventBus、AuditService、ApplicationContext、SafetyKernel、seed/MySQL 仓储、SimulationDeviceFacade、Dobot 客户端。",
        "解析 config/defaults.ini 和 db/schema/mysql_5_7_init.sql。若 MySQL 启用且连接成功，则执行 schema；空患者表时从 seed 引导。",
        "若最终不是 MySQL 仓储，程序弹出“数据库认证不可用”并 return 1。注意：这使 database/enabled=false 的旧离线方案失效。",
        "初始化登录表；显示医生登录/注册对话框；取消登录则正常退出。",
        "登录后启动仿真设备；启动 USB 摄像头；每 5 秒 watchdog 检查，超过 8 秒无首帧或帧陈旧则重启。",
        "按 display 配置创建三屏窗口；不足三屏且允许回退则创建单窗口 MainWindow。",
    ])
    add_heading(doc, "5.2 医生登录与账户", 2)
    add_table(
        doc,
        ["功能", "输入/参数", "实现"],
        [
            ["登录", "username、password", "用户名 trim + lower；校验 is_active；成功更新 last_login_at"],
            ["注册医生", "姓名、科室、职称、执业证号、账号、密码、确认密码", "姓名/账号必填；密码至少 8 位；创建 doctor_profile + user_account；默认角色 physician"],
            ["口令存储", "PBKDF2-HMAC-SHA256", "120,000 次迭代、16 字节随机盐、32 字节散列，URL-safe Base64，常量时间比较"],
            ["角色", "operator/physician/engineer/administrator", "数据库有四角色；当前自助注册只生成 physician；页面级授权尚不完整"],
        ],
    )
    add_heading(doc, "5.3 三屏映射", 2)
    add_table(
        doc,
        ["角色", "默认 auto 几何选择", "承载页面", "顶部附加入口"],
        [
            ["Dashboard", "最左侧屏幕", "DeviceMonitorPage；内含数据管理切换", "医生、系统模式、联锁、数据管理子菜单、退出"],
            ["Planning", "剩余屏幕中位置靠下/右的规划槽位（以源码分配为准）", "PlanningPage", "医生习惯折叠配置、退出"],
            ["Treatment", "剩余治疗槽位", "TreatmentPage", "医生、系统模式、联锁、退出"],
        ],
    )
    add_paragraph(doc, "现场必须运行 PanTheraConsole.exe --list-screens，确认 Qt 屏幕序号、geometry 与 Windows 排列；必要时把 dashboard_screen/planning_screen/treatment_screen 改成显式索引。fullscreen=true 时各窗口全屏。少于三屏且 fallback_single_window=true 时，单窗口提供设备监控、治疗计划、治疗、数据管理四个导航项。")

    add_heading(doc, "6. 页面一：设备监控屏（Dashboard）", 1)
    add_paragraph(doc, "定位：硬件接入、设备启停、机械臂与三轴调试、实时状态和工程日志的主入口。该屏还承载数据管理子模块。页面同时显示 SimulationDeviceFacade 的快照和真实设备控件，因此接手时必须区分“仿真指标”和“真实协议回包”。")
    add_heading(doc, "6.1 页面功能总表", 2)
    add_table(
        doc,
        ["模块", "可见功能", "关键参数", "接口/输出", "当前状态"],
        [
            ["全局设备指标", "电压、电流、功率、温度、水路、运动、急停、超声可用性", "simulation timer 1000 ms", "SimulationDeviceFacade::snapshot/healthSignalsChanged", "可见；主要为仿真"],
            ["水泵/水路", "回水泵、供水泵独立启停；上水；循环；停全部；液位日志", "COM3/9600；泵 0x02/0x03；循环 200 mL/min；上水 350 mL/min；目标 0–43 cm，默认 30", "Modbus RTU", "可见；待实机验收"],
            ["RO/正反转泵", "正转、反转、停止", "Dobot DO13/DO14", "Dashboard TCP DO(index,state)", "可见；待实机验收"],
            ["真空泵", "开/关", "UIM 节点8 P4；1=通，0=断", "UISimCanFunc.dll 数字输出", "可见；待实机验收"],
            ["温控", "连接、设定温度、读温度、实时轮询", "COM3/9600；地址4；CH1 UI；2秒刷新", "LU-926 Modbus RTU", "可见；待实机验收"],
            ["液位/限位", "液位读取；水箱高/低位显示；低位停加热", "COM3/9600；液位地址1；Node6 S3 高位、Node7 S3 低位；3样本消抖", "Modbus + UIM 传感器", "可见；待实机验收"],
            ["RIGOL 信号源", "搜索/连接、占空比、输出开关", "USB?*::INSTR；RIGOL IDN；CH1；1.186 MHz；2 Vpp；50Ω；占空比1–99%，默认50%", "VISA/SCPI", "可见；依赖 VISA runtime"],
            ["Aigtek 功放", "连接、增益、输出开关", "COM5/9600 8N1；增益0.1–30，默认30", "ASCII 串口", "可见；待实机验收"],
            ["Dobot", "连接、控制权、上电、使能、拖拽、回安全原点、Z轴对齐、轨迹、清错", "192.168.5.1:29999；3s；运动限速≤10%，加速度默认1%", "Dashboard/Motion TCP + SFTP", "可见；高风险实机功能"],
            ["三轴 UIM", "搜索/打开、使能、回零、绝对/点动/持续运动、软急停、传感器", "节点6/7/8；1A；速度2400；1秒刷新", "UISimCanFunc.dll/USB-CAN", "可见；待实机验收"],
            ["数据管理", "患者、影像、治疗记录、报告、证据文档", "MySQL + filesystem", "IClinicalDataRepository + workflow repository", "可见（Dashboard 子菜单/单窗独立页）"],
        ],
        font_size=7.8,
    )
    add_heading(doc, "6.2 水路与温控详细行为", 2)
    add_bullets(doc, [
        "水泵、温控、液位默认共用固定 COM3/9600。页面为避免并发占用，会在连接某一设备前关闭同一共享串口上的其他客户端；因此三者不是同时独立持有 COM3 的架构。",
        "供水泵默认 Modbus 地址 0x03，回水泵 0x02；启停使用线圈 0x0001（FC05），方向线圈为 0x0002。流量、持续时间、实时流量使用 FC03/FC10 读写保持寄存器。",
        "上水目标以 cm 输入，默认 30、最大 43；上水流量 350 mL/min。循环同时启动 0x03/0x02，默认各 200 mL/min。",
        "温控协议支持 6 通道，当前设备监控 UI 主要操作 CH1；温度以 0.1°C 原始单位传输。实时轮询间隔 2秒。",
        "低液位限位触发后代码会把温控设定值写为 0，作为停止加热措施；该逻辑依赖 Node7 S3 传感器有效并连续 3 次采样。",
        "液位传感器默认地址 0x01，液位寄存器 0x0000；地址探测使用广播/探测地址 0xEE、功能码 0x07、寄存器 0x0004。",
    ])
    add_heading(doc, "6.3 RIGOL 与 Aigtek 功率链", 2)
    add_table(
        doc,
        ["设备", "连接", "主要命令/数据", "成功判据", "依赖/限制"],
        [
            ["RIGOL", "VISA 动态库；USB 资源 USB?*::INSTR；*IDN? 含 RIGOL", "清状态；CH1 脉冲；1.186 MHz；2 Vpp；50Ω；Duty；OUTP CH1 ON/OFF；查询", "VISA 返回成功并可查询", "必须安装 NI-VISA/Keysight/RIGOL UltraSigma 之一；本机未实机验证"],
            ["Aigtek", "COM5、9600、8N1、无流控", "ATA,SET,AON,0000; / ATA,SET,AOF,0000; / ATA,SET,AMP,xxxx; + CRLF；增益×10后4位补零", "响应包含 SET DONE!", "增益协议0–30；UI 0.1–30；无与治疗页功率闭环联动"],
        ],
    )
    add_heading(doc, "6.4 Dobot 机械臂", 2)
    add_table(
        doc,
        ["功能", "接口与参数", "安全/异常处理"],
        [
            ["连接", "host 默认192.168.5.1；commandPort=29999；motionPort=29999；timeout=3000 ms", "连接失败更新状态；断开时尝试停脚本、Stop、DisableRobot"],
            ["控制/使能", "RequestControl、PowerOn、EnableRobot；payload preset TEST 或7.5 kg显式参数", "payload_apply_before_enable=true；不同控制器固件需核对命令签名"],
            ["拖拽", "StartDrag/StopDrag；物理拖拽按钮 DI13，每80 ms轮询", "RobotMode 5/6/7/8/9 用于识别使能、拖拽、运行、单步、错误"],
            ["物理电源按键", "长按判定2500 ms", "与上电/下电动作联动；须现场核对电气接线"],
            ["安全原点", "MovJ 到 [568.4855,-652.5055,753.2592,-179.9999,0,-152.9531]，user/tool=0", "大动作前确认；速度≤10%"],
            ["Z轴对齐", "Rx=180、Ry=0、Rz默认保持当前；逆解后运动；最大关节变化180°", "120s超时、200ms轮询、连续3次≤1°才稳定；速度/加速度受限"],
            ["安全墙", "name=testSafe、index=1、每1000 ms轮询", "报警锁存、清错/恢复模式；必须按现场固件验证"],
            ["轨迹", "SFTP /dobot/userdata/project/process/trajectory/；StartPath(pathType=1,isConst=0,multi=1,sample=50,freq=0.2,user=-1,tool=-1)", "先列目录/选轨迹、确认、移动到起点，再执行；具备 Stop/Pause/Continue/Recovery"],
        ],
        font_size=8,
    )
    add_heading(doc, "6.5 UIM 三轴", 2)
    add_table(
        doc,
        ["轴/节点", "机械意义", "允许范围", "换算", "方向与传感器"],
        [
            ["Node 6", "左右摆动", "-19,290…19,062 steps", "1,777.8 steps/°；治疗时相对中心±10°", "左摆/右摆；回零到零位；水箱高位读取 S3"],
            ["Node 7", "左右/层间移动", "0…74,461 steps", "6,400 steps/cm = 640 steps/mm", "负向左移、正向右移；回零向 S2；水箱低位 S3；计划采层轴"],
            ["Node 8", "上下移动", "0…152,314 steps", "640 steps/mm；约237.99 mm行程", "负向上移、正向下移；回零向 S2；P4 控真空泵"],
        ],
    )
    add_bullets(doc, [
        "SDK：动态加载 UISimCanFunc.dll；搜索/打开 USB-CAN 网关，选择节点后发送 Enable/Disable/Home/Current/Speed/Step/POS/QEC/P4 等调用。",
        "默认电流各 1.0 A，速度 2400；状态刷新 1秒；一次位移超过 16,000 步（5圈×3,200步）弹确认。",
        "一键回零超时 120秒，每100 ms轮询；Node7/8 依赖 S2，Node6 回零到0；限位动作值3表示减速停止。",
        "软件急停将速度置0并取消持续点动，不等同于切断驱动器电源；同时向仿真门面注入 EmergencyStop。释放后恢复速度。",
    ])
    add_heading(doc, "6.6 数据管理子模块", 2)
    add_table(
        doc,
        ["对象", "功能", "字段/输入", "删除/输出"],
        [
            ["患者", "搜索、选择、新建、编辑、归档、CSV批量导入", "id、name、age、gender、contact、diagnosis", "归档写 deleted_at 并从列表隐藏；非物理删除"],
            ["CSV", "UTF-8、支持引号转义和逗号", "标题别名：id/患者id，name/患者姓名/patientname，age/患者年龄/patientage，gender/性别，diagnosis/诊断结果，contact/联系方式/phone；无表头时支持固定列", "逐行统计成功/失败并提示首个错误"],
            ["影像序列", "列表、新建、编辑、删除、缩略图", "id、patient_id、type、storage_path、acquisition_date、notes", "数据库元数据与本地路径；删除前核对文件处理策略"],
            ["治疗记录", "列表、新建/编辑/删除、查看证据", "session id/date/plan/lesion/energy/duration/path/dose/status", "可打开 medical_evidence_document 的 DOCX 路径；缺失时尝试恢复登记"],
            ["治疗报告", "新建、编辑、删除、HTML预览", "patient、session、generated_at、title、content_html、notes", "保存至 MySQL；HTML 内容当前无专用模板版本控制"],
        ],
        font_size=8,
    )

    add_heading(doc, "7. 页面二：治疗计划屏（Planning）", 1)
    add_paragraph(doc, "定位：选择患者与采集路径，驱动层轴获取 USB 超声视频帧，完成当前/历史图像标注、对齐、三维重建、靶点生成、功率与剂量评估，并保存/批准 TherapyPlan。")
    add_heading(doc, "7.1 页面操作流程", 2)
    add_numbered(doc, [
        "从 MySQL 患者列表选择患者；未选患者不能建立计划。",
        "添加采集路径时读取 Dobot 当前位姿并形成路径项；可删除不需要的路径。",
        "设置采集层数、层间步长和总长度；启动采集前检查 UIM Node7、S1/S2 与当前位置范围。",
        "每层驱动 Node7 到目标步数，等待停止，取 ApplicationContext 中最新 USB 摄像头帧并保存 PNG，同时记录 Node7 绝对位置与机械臂位姿。",
        "用红/蓝/绿/橙画笔标注；可撤销/清空；可加载历史图像并用四个锚点建立当前/历史层同步映射。",
        "根据标注轮廓重建三维体积；选择点治疗或线治疗；设置行距、驻留、功率、呼吸跟踪；逐层生成靶点。",
        "查看靶点数、预计体积、消融体积、覆盖率、功率曲线和计划预览；保存草稿或批准计划。",
    ])
    add_heading(doc, "7.2 影像采集参数", 2)
    add_table(
        doc,
        ["参数/行为", "当前值或范围", "实现细节"],
        [
            ["采集层数", "1–60；默认20", "每层形成 StagedSliceState 与一张 PNG"],
            ["层间步长", "1–20 mm；默认1 mm", "Node7：640 steps/mm"],
            ["层轴范围", "0(S2)–74,461(S1) steps", "超界即拒绝；按起点+层数×步长预检查"],
            ["电机速度", "2400", "与设备页三轴速度一致"],
            ["启动运动超时", "3,000 ms", "未观察到位置/编码器变化则失败"],
            ["停止等待", "最长90–120秒级（代码按采集常量）", "每200 ms左右轮询；检查稳定与目标容差"],
            ["相机", "USB3 PLUS Video 共享帧", "无帧则本层失败；主进程 watchdog 5秒/8秒阈值"],
            ["保存", "runtime 图像根目录下 PNG", "自动建立目录；记录 layer/slice、Node7 绝对步数、Dobot pose、采集参数"],
        ],
    )
    add_heading(doc, "7.3 图像、标注与对比", 2)
    add_bullets(doc, [
        "当前治疗影像和历史影像各有独立层滑块、缩放、标尺；当前影像可弹出全屏对话框。",
        "标注颜色为红、蓝、绿、橙；数据保存为 strokes 数组，每条包含 color 与 points。支持撤销上一笔和清空当前层。",
        "历史图像支持 png/jpg/jpeg/bmp 本地导入，也可从既往治疗持久化数据恢复。",
        "对比同步需要当前序列左起/右止与历史序列左起/右止四个锚点；按比例将同步滑块映射到两个序列的层索引。",
        "三维重建优先使用标注轮廓；无完整轮廓时会构造推断轮廓，仅适合原型预览，不能替代分割验证。",
        "继续治疗工作流可把上一治疗会话的采集影像和标注导入新草稿，并登记 treatment_continue_import。",
    ])
    add_heading(doc, "7.4 计划参数、靶点与评估", 2)
    add_table(
        doc,
        ["参数", "范围/默认", "影响"],
        [
            ["治疗模式", "直接治疗；保留分段数据结构但当前分段控件隐藏", "决定计划预览和段组织"],
            ["路径模式", "Point / Line", "点阵或线扫靶点；消融估计因子不同"],
            ["行/点间距", "0.5–10.0 mm；默认3.0", "修改后会使已生成靶点失效，必须重新生成"],
            ["驻留时间", "0.1–10.0 s；默认0.3", "写入 plan/point；治疗页计算能量使用，但定时推进当前固定450 ms"],
            ["计划功率", "20–800 W；默认400", "写入计划与评估；治疗页真实设备输出尚未接通"],
            ["呼吸跟踪", "开/关", "依据标注轮廓的基线/实时质心偏移生成 correctedTargets；没有外部呼吸传感器闭环"],
            ["预计体积", "标注面积 × 层距 / 1000 → cm³", "多层求和；依赖像素到毫米标定"],
            ["预计消融体积", "Line×0.82；Point×0.62", "经验系数；需临床/声学验证"],
            ["覆盖率", "clamp(消融体积/预计体积)", "仅规划估算指标"],
        ],
    )
    add_heading(doc, "7.5 TherapyPlan 输出字段", 2)
    add_table(
        doc,
        ["对象", "字段"],
        [
            ["TherapyPlan", "id、patientId、name、pattern、approvalState、segments、plannedPowerWatts、spacingMm、dwellSeconds、respiratoryTrackingEnabled、deliveryMode、坐标/深度、createdAt、approvedAt、approvedBy、robot pose"],
            ["TherapySegment", "id、orderIndex、label、plannedDurationSeconds、points、sourceSliceIndex、axis7PositionSteps、sourceImagePath"],
            ["TherapyPoint", "index、QPointF(mm)、dwellSeconds、powerWatts、line/segment 元数据"],
            ["annotation_payload", "strokes[{color,points}]、spacing_mm、dwell_seconds、power_w、pattern、delivery_mode、target_count，以及面积/体积/呼吸补偿信息"],
        ],
    )
    add_callout(doc, "已知缺陷", "批准计划时 approvedBy 当前存在写死为“physician”的路径，而数据库 therapy_plan.approved_by 外键要求 user_account.id。接手后应改成 ApplicationContext.currentOperator.userId，并补迁移/回归测试。", fill="FCE4D6")
    add_heading(doc, "7.6 个性化界面", 2)
    add_paragraph(doc, "计划页有多块可折叠区域（患者、路径、采集、三维模型、能量图、计划详情、图像操作、治疗参数、功率评估、评估指标）。通过 QSettings 保存“全折叠”“全展开”和医生自定义命名配置；三屏顶部“医生习惯”菜单调用这些配置。")

    add_heading(doc, "8. 页面三：治疗执行屏（Treatment）", 1)
    add_paragraph(doc, "定位：加载已批准/锁定的计划，叠加显示实时 USB 摄像头和计划靶点，按层驱动 UIM 三轴到每个治疗点，提供开始/暂停/继续/停止当前层/终止全部治疗，并持久化治疗结果和证据。")
    add_heading(doc, "8.1 当前可见功能", 2)
    add_table(
        doc,
        ["区域", "功能", "约束/参数"],
        [
            ["影像区", "实时 USB 相机背景、当前层靶点覆盖、层切换", "取 ApplicationContext 共享帧；计划图像/标注用于叠加"],
            ["计划选择", "按当前患者列出计划", "只允许 Approved/Locked；过滤 seed/导入占位计划"],
            ["层控制", "上一层/下一层/滑块、开始/暂停/继续/停止当前层", "执行中锁定部分选择；SafetyKernel 统一判定"],
            ["进度/时间", "进度条、本层剩余/总时间、全计划剩余/总时间", "按点数×dwell 估算显示；实际推进定时器固定450 ms"],
            ["三维查看", "重建并预览计划轮廓/靶点", "原型可视化"],
            ["终止", "取消、继续治疗、终止整个治疗", "继续会保存当前状态并回到计划页；终止会生成完整报告和证据"],
            ["日志", "记录选层、运动、输出、暂停、中止和异常", "内存 UI 日志上限120块；关键结果写 MySQL"],
        ],
    )
    add_heading(doc, "8.2 单层执行算法", 2)
    add_numbered(doc, [
        "读取当前患者与选中计划；验证状态为 Approved/Locked；调用 SafetyKernel.requestTreatmentStart。",
        "建立执行点序列：点治疗按蛇形顺序排列，降低跨行空行程。",
        "加载 UISimCanFunc.dll、打开网关并确认 Node6/7/8；速度设为2400。",
        "把 Node7 移动到 TherapySegment.axis7PositionSteps（计划采集时保存的绝对层位置）。",
        "读取 Node6 当前位置作为摆动中心；每个治疗点先移动 Node8 垂直位置，再移动 Node6 相对摆角。",
        "运动前把仿真 outputEnabled=false；运动结束后置 true；固定450 ms后累计能量并推进下一点。",
        "本层完成后关闭仿真输出，记录层能量与进度；用户可选择下一层或终止。",
    ])
    add_heading(doc, "8.3 三轴坐标换算与保护", 2)
    add_table(
        doc,
        ["项目", "值", "说明"],
        [
            ["层轴", "Node7，0…74,461 steps", "直接使用计划段保存的 axis7PositionSteps"],
            ["垂直轴", "Node8，0…152,314 steps", "Y(mm)×640；最大约237.99 mm"],
            ["摆动轴", "Node6，以开始层时当前位置为中心", "X(mm) 经超声几何转换为角度，再×1,777.8 steps/°；限制±10°"],
            ["位置容差", "20 steps", "进入容差视为到位"],
            ["边界容差", "160 steps", "轻微越界可夹紧；超过则失败"],
            ["轮询", "160 ms", "同步等待电机位置"],
            ["启动超时", "3,000 ms", "未观测到移动则失败"],
            ["单次移动超时", "120,000 ms", "超时中止当前操作"],
            ["功率/驻留", "energy += powerWatts × dwellSeconds", "能量按计划值计算，不是实测声功率积分"],
        ],
    )
    add_callout(doc, "关键闭环缺口", "治疗页的开/关输出调用 SimulationDeviceFacade，不直接控制设备监控页中的 RIGOL/Aigtek。且每个点由450 ms定时器推进，未按用户设置的 dwellSeconds 实际等待。当前“已交付”的治疗执行只能视为运动/流程原型，不能视为真实能量治疗闭环。", fill="FCE4D6")
    add_heading(doc, "8.4 暂停、停止、报警与终止", 2)
    add_bullets(doc, [
        "暂停：SafetyKernel.pauseTreatment，停止进度定时器并关闭仿真输出；继续时重新校验全部联锁。",
        "停止当前层：停止定时器、关闭仿真输出、保留其他层和已完成能量，可重新开始或切层。",
        "红色联锁：SafetyKernel 转 Alarm 并发出 treatmentAbortRequested，治疗页执行中止路径。",
        "终止全部治疗：弹出自定义对话框。选择“继续治疗”会保存阶段结果并把历史影像/标注交给计划页继续；选择“终止”会形成最终报告和证据文档。",
    ])
    add_heading(doc, "8.5 结束时持久化", 2)
    add_table(
        doc,
        ["数据", "表/文件", "内容"],
        [
            ["治疗会话", "treatment_session", "患者、计划、开始/结束、病灶、总能量、总时长、路径摘要、剂量、最终状态"],
            ["点记录", "treatment_record", "segment_index、point_index、delivered_energy_j、delivered_dose、executed_at"],
            ["影像/标注快照", "treatment_image_capture / treatment_image_annotation", "层图、机器人位姿、标注 payload、面积/体积、保存原因"],
            ["最终设备快照", "device_snapshot 及工作流状态表", "当前主要来自仿真；真实设备状态表有结构但需核查写入覆盖率"],
            ["报告", "treatment_report", "仅完整终止时生成 HTML 报告"],
            ["证据文档", "runtime/evidence_documents/<session>/<session>.docx", "操作证据摘要、SHA-256 校验，登记 medical_evidence_document"],
            ["终止证据", "treatment_termination_log", "continue/terminate、最终状态、原因、医生/用户/登录会话、summary payload"],
        ],
        font_size=8,
    )
    add_heading(doc, "8.6 已实现但当前不可见的水路/温控卡片", 2)
    add_paragraph(doc, "TreatmentPage 中存在 createFluidControlCard() 及完整水泵、液位、温控、Dobot DO13/14 水箱控制代码，但构造函数当前没有把该卡片 addWidget 到可见布局。下列能力属于“代码已实现未接入”，不能对外宣称为治疗页已可操作：")
    add_bullets(doc, [
        "水泵/液位/温控串口可选；6个温控通道；目标温度0–45°C，默认37°C，每3秒刷新。",
        "水箱2目标液位默认300 mm、最大430 mm、±10 mm循环容差；补水流量350 mL/min；循环流量200 mL/min。",
        "水箱1上限位为 Node6 S2，高有效，1秒消抖；Dobot DO13正转、DO14反转。",
        "停止全部流体设备、填充/排水/循环、温控启停与异常停机函数均存在，但当前操作员无法从治疗页进入。",
    ])

    add_heading(doc, "9. 三个页面之间的接口与共享参数", 1)
    add_table(
        doc,
        ["生产者", "共享接口/数据", "消费者", "参数/触发"],
        [
            ["登录对话框", "ApplicationContext.currentOperator", "三个页面、审计、证据", "userId、doctorId、username、fullName、department、title、license、role"],
            ["数据管理/计划", "selectedPatientChanged", "SafetyKernel、计划、治疗", "PatientRecord；清空触发 selectedPatientCleared"],
            ["计划", "activePlanChanged", "SafetyKernel、治疗", "TherapyPlan；approvalState 必须 Approved/Locked"],
            ["USB摄像头", "updateTreatmentCameraFrame", "计划采集、治疗显示", "QImage + activeCameraDescription；watchdog 负责重启"],
            ["SimulationDeviceFacade", "snapshotUpdated/healthSignalsChanged", "Dashboard、SafetyKernel、治疗", "1秒快照；water/power/motion/temp/e-stop/ultrasound 六类健康"],
            ["SafetyKernel", "safetySnapshotChanged/systemModeChanged", "三屏标题栏、TreatmentPage", "Green/Yellow/Red；canStart；message；activeInterlocks"],
            ["TreatmentPage", "继续治疗上下文/持久化", "PlanningPage", "previous_session、影像、标注、导入原因"],
            ["MySQL repository", "IClinicalDataRepository", "Data/Planning/Treatment", "患者、影像、计划、会话、报告 CRUD"],
        ],
        font_size=8,
    )

    add_heading(doc, "10. 外部硬件与软件接口总表", 1)
    add_table(
        doc,
        ["接口", "物理/协议", "默认连接", "主要参数", "代码位置"],
        [
            ["Dobot 控制", "TCP 文本命令；Dashboard + Motion 双 socket", "192.168.5.1:29999/29999，3s", "RequestControl、PowerOn、EnableRobot、Stop/Pause/Continue、DI/DO、MovJ/MovL、StartPath、RobotMode/GetPose/GetAngle/GetErrorID", "src/adapters/dobot"],
            ["Dobot 轨迹", "SFTP/libssh2", "192.168.5.1:22，root/dobot", "/dobot/userdata/project/process/trajectory/；5s", "dobot_trajectory_sftp_client"],
            ["UIM 三轴", "USB-CAN + 厂商 DLL", "UISimCanFunc.dll；节点6/7/8", "Enable/Disable/Home/Current/Speed/Step/POS/QEC/P4/S1-S3", "uim_motor_gateway"],
            ["水泵", "RS485 Modbus RTU", "COM3，9600；地址0x02/0x03", "FC03/05/10；0–1500 mL/min；0–86400s", "water_pump_modbus_client"],
            ["LU-926 温控", "RS485 Modbus RTU", "COM3，9600；地址4", "6通道；FC03/06；0.1°C；UI实时CH1", "lu926_temperature_*"],
            ["液位", "RS485 Modbus RTU", "COM3，9600；地址1", "level reg0x0000；address probe 0xEE/FC07/reg0x0004", "liquid_level_modbus_client"],
            ["Aigtek 功放", "RS232/USB串口 ASCII", "COM5，9600 8N1", "AON/AOF/AMP；0–30倍", "aigtek_power_amplifier_client"],
            ["RIGOL 信号源", "USB VISA/SCPI", "USB?*::INSTR", "CH1、Pulse、1.186MHz、2Vpp、Duty1–99%、50Ω", "rigol_visa_client"],
            ["超声相机", "Windows DirectShow", "USB3 PLUS Video", "帧共享；5s watchdog；8s无帧重启", "usb_camera_frame_source"],
            ["MySQL", "QMYSQL/TCP", "127.0.0.1:3306/panthera_sys", "root/123456；connect timeout2s；utf8mb4", "src/adapters/mysql"],
            ["文件系统", "NTFS/普通路径", "runtime/images、reports、evidence_documents", "需可写；交付包不得含真实患者数据", "config/defaults.ini"],
        ],
        font_size=7.7,
    )
    add_heading(doc, "10.1 水泵 Modbus 寄存器", 2)
    add_table(doc, ["功能", "地址", "类型/编码", "功能码"], [
        ["设定流量", "0x4001", "float，两个16位字交换；0–1500 mL/min", "FC10写2寄存器；FC03读"],
        ["设定运行时长", "0x4005", "uint32毫秒，字交换；0–86400s", "FC10写2寄存器"],
        ["实时流量", "0x4007", "float，字交换", "FC03读2寄存器"],
        ["启停", "coil 0x0001", "FF00=ON、0000=OFF", "FC05"],
        ["方向", "coil 0x0002", "正/反（现场核对 clockwise 定义）", "FC05"],
    ])
    add_heading(doc, "10.2 LU-926 寄存器", 2)
    add_table(doc, ["数据", "CH1基址", "说明"], [
        ["设定值 SET", "0x0000", "每通道按协议偏移；写FC06；温度×10"],
        ["当前设定", "0x0100", "读设定显示"],
        ["输出", "0x0108", "输出相关状态"],
        ["过程值 PV", "0x0110", "读FC03；温度×0.1°C"],
        ["报警", "0x0116", "报警状态"],
        ["冷端补偿", "0x0128", "协议字段"],
        ["故障", "0x0129", "故障码"],
        ["原始范围", "-1999…9999", "对应 -199.9…999.9°C；页面使用更窄业务范围"],
    ])
    add_heading(doc, "10.3 Dobot 命令能力", 2)
    add_paragraph(doc, "适配器公开的主要命令包括：RequestControl、PowerOn、EnableRobot（预设/显式负载）、DisableRobot、ClearError、Stop、Pause、Continue、EmergencyStop、RunScript/StopScript/PauseScript/ContinueScript、GetStartPose、StartPath、PathRecovery、Sync、OffsetPara、RunTo、SpeedFactor、User、Tool、AccJ/AccL、SpeedJ/SpeedL、VelJ/VelL、RobotMode、GetErrorID、GetPose、GetAngle、GetCurrentCommandID、DI、DO、ToolDI、ToolDO、MovJ、MovL。每条命令均需按目标 Dobot 控制器固件版本核对返回格式、权限与坐标系。")

    add_heading(doc, "11. 数据库、认证与数据字典", 1)
    add_heading(doc, "11.1 数据库连接与初始化", 2)
    add_table(doc, ["参数", "当前默认值", "说明"], [
        ["driver", "QMYSQL", "Qt sqldrivers/qsqlmysql.dll；还需匹配架构的 MySQL client DLL"],
        ["host/port", "127.0.0.1 / 3306", "连接超时2秒"],
        ["schema", "panthera_sys", "数据库必须预先存在，随后程序可执行建表 SQL"],
        ["账号", "root / 123456", "仅开发默认；必须改为应用专用账号并轮换"],
        ["字符集", "utf8mb4 / utf8mb4_unicode_ci", "SQL 初始化数据库与认证动态表"],
        ["启动策略", "MySQL 必需", "连接/建表/登录表初始化任一失败均阻止进入主界面"],
        ["空库引导", "patient 表为空时从 SeedClinicalDataRepository 导入", "须防止生产库误导入示例数据"],
    ])
    add_heading(doc, "11.2 表目录", 2)
    purposes = {
        "role": "角色字典",
        "doctor_profile": "医生专业档案",
        "user_account": "登录账户与密码散列",
        "patient": "患者主数据（软删除）",
        "image_series": "患者影像序列元数据",
        "therapy_plan": "治疗计划主表与序列化 payload",
        "therapy_segment": "计划分层/分段",
        "treatment_session": "一次治疗会话汇总",
        "treatment_record": "逐段逐点执行记录",
        "treatment_report": "HTML治疗报告",
        "device_snapshot": "设备遥测快照",
        "alarm_log": "报警日志",
        "audit_log": "通用审计日志",
        "config_profile": "配置版本与审批",
        "doctor_login_session": "医生登录会话",
        "clinical_operation_log": "页面/模块/功能级临床操作证据",
        "device_control_event": "设备命令请求/结果证据",
        "water_pump_status": "水泵/水路状态快照",
        "temperature_control_status": "温控状态快照",
        "liquid_level_status": "液位状态快照",
        "robot_arm_status": "机械臂状态与位姿",
        "three_motor_status": "UIM三轴状态",
        "power_device_status": "RIGOL/Aigtek及功率状态",
        "robot_acquisition_path": "机器人采集路径",
        "historical_treatment_image": "既往治疗影像",
        "treatment_image_capture": "当前治疗分层采集影像与位姿",
        "treatment_image_annotation": "标注、掩模、面积/体积与payload",
        "treatment_continue_import": "继续治疗的标注导入关系",
        "therapy_plan_evaluation": "计划预计/已治体积和进度评估",
        "treatment_termination_log": "终止/继续治疗证据",
        "medical_evidence_document": "DOCX证据路径与SHA-256",
    }
    sql_text = (ROOT / "db" / "schema" / "mysql_5_7_init.sql").read_text(encoding="utf-8")
    schema_tables = parse_schema(sql_text)
    add_table(doc, ["序号", "表名", "用途", "字段数"], [[i + 1, name, purposes.get(name, "见字段定义"), len(columns)] for i, (name, columns, _) in enumerate(schema_tables)], font_size=8)
    add_heading(doc, "11.3 字段级数据字典", 2)
    add_paragraph(doc, "以下定义直接从 db/schema/mysql_5_7_init.sql 解析。MEDIUMTEXT 字段主要承载 JSON/序列化 payload；接手后应补齐 payload 的 JSON Schema 与迁移版本号。")
    for i, (name, columns, constraints) in enumerate(schema_tables, start=1):
        add_heading(doc, f"11.3.{i} {name} — {purposes.get(name, '数据表')}", 3)
        rows = []
        for col, definition in columns:
            nullable = "否" if "NOT NULL" in definition.upper() or "PRIMARY KEY" in definition.upper() else "是"
            default_match = re.search(r"\bDEFAULT\s+([^ ]+(?:\s+[^ ]+)?)", definition, re.I)
            default = default_match.group(1) if default_match else "—"
            rows.append([col, definition, nullable, default])
        add_table(doc, ["字段", "SQL定义", "可空", "默认/备注"], rows, font_size=7.5, widths=[3.0, 9.1, 1.2, 2.7])
        if constraints:
            add_paragraph(doc, "约束/索引：")
            add_bullets(doc, constraints, level=1)

    add_heading(doc, "12. 配置文件全量说明", 1)
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read(ROOT / "config" / "defaults.ini", encoding="utf-8")
    cfg_rows = []
    for section in config.sections():
        for key, value in config.items(section):
            risk = ""
            if "password" in key.lower():
                risk = "敏感：明文；交接后轮换"
            elif section == "display":
                risk = "现场按实际屏幕核对"
            elif section == "dobot" and key.startswith("safe_"):
                risk = "运动安全参数；实机标定后审批"
            cfg_rows.append([section, key, value, risk])
    add_table(doc, ["段", "键", "当前值", "交接备注"], cfg_rows, font_size=7.5, widths=[2.3, 5.0, 5.2, 3.5])
    add_callout(doc, "配置事实", "deployment.example.ini 当前更多是未来硬件抽象模板，主程序没有把 device_mode=simulation 等项作为统一开关。实际行为主要由 defaults.ini 与页面硬编码常量决定。", fill="FFF2CC")

    add_heading(doc, "13. 文件存储、报告与证据", 1)
    add_table(doc, ["路径", "内容", "生命周期/注意"], [
        ["runtime/images", "采集图、标注图、历史图、缩略图", "须备份；不得随源码交付真实患者数据；删除数据库记录不一定删除文件"],
        ["runtime/reports", "报告输出预留目录", "当前报告主体保存在 MySQL HTML字段，目录用途需统一"],
        ["runtime/evidence_documents/<session>", "治疗操作证据 DOCX", "完全终止生成；数据库登记路径与SHA-256；继续治疗可能只有阶段数据无最终DOCX"],
        ["config/defaults.ini", "运行配置与当前明文口令", "应迁移到站点私有配置、最小ACL、脱离Git"],
        ["db/schema/mysql_5_7_init.sql", "建表脚本", "当前无显式 schema_version/migration 序号；上线前建立迁移体系"],
    ])
    add_paragraph(doc, "MedicalEvidenceDocumentWriter 直接构造最小 OOXML DOCX 包（word/document.xml 等），用于临床操作证据。生成后计算 SHA-256 写 medical_evidence_document。该校验只能发现文件变化，不能证明签名人身份；若用于受控记录，后续需加数字签名、可信时间戳、不可变存储和审核流程。")

    add_heading(doc, "14. 构建、测试与本机验证记录", 1)
    add_heading(doc, "14.1 本次验证结论", 2)
    add_table(doc, ["验证项", "结果", "解释"], [
        ["当前工作区主程序编译", "通过", "cmake --build build/mingw_tests --target PanTheraConsole --parallel 4；main.cpp 重编译并成功链接"],
        ["Qt部署", "基本完成，有警告", "windeployqt 复制 Qt6Core/Gui/Network/SerialPort/Sql/Svg/Widgets、platform/imageformat/sql/tls插件"],
        ["vcpkg applocal", "存在警告", "找不到 dumpbin/llvm-objdump/objdump，未能解析非Qt DLL依赖；进程最终 exit 0"],
        ["已有测试二进制 CTest", "11/13通过", "设置每项15秒并行运行；影像算法失败；计划页回归超时"],
        ["当前源码全量 tests 重新链接", "未完成", "多个测试可执行文件链接步骤 g++.exe 无诊断返回1；主目标随后可链接。需新干净构建目录复现"],
        ["真实硬件", "未验证", "本次无水泵、温控、液位、RIGOL、Aigtek、Dobot、UIM、摄像头实机验收"],
        ["三屏人工验收", "未执行", "启动强制 MySQL+登录，且本次任务以静态审计/构建为主"],
    ], font_size=8.2)
    add_heading(doc, "14.2 13 个测试目标与结果", 2)
    test_results = [
        ("panthera_core_tests", "通过", "SafetyKernel 状态/联锁"),
        ("panthera_application_context_tests", "通过", "共享上下文"),
        ("panthera_clinical_data_service_tests", "通过", "临床数据服务"),
        ("panthera_dobot_tcp_client_tests", "通过", "TCP命令/解析"),
        ("panthera_water_pump_modbus_client_tests", "通过", "水泵帧"),
        ("panthera_aigtek_power_amplifier_client_tests", "通过", "Aigtek命令"),
        ("panthera_lu926_temperature_modbus_client_tests", "通过", "温控帧"),
        ("panthera_liquid_level_modbus_client_tests", "通过", "液位帧"),
        ("panthera_simulation_device_facade_tests", "通过", "仿真设备"),
        ("panthera_mysql_clinical_data_integration_tests", "CTest显示通过", "测试代码允许环境不足时跳过；需查日志确认是否实际连库"),
        ("panthera_therapy_imaging_algorithms_tests", "失败", "CTest无详细输出；需单独 -V/调试"),
        ("panthera_planning_page_regression_tests", "15秒超时", "残留进程曾占用测试exe，本次已结束；测试需修复退出"),
        ("panthera_login_dialog_tests", "通过", "登录对话框"),
    ]
    add_table(doc, ["测试", "本机结果", "范围/备注"], test_results, font_size=8)
    add_heading(doc, "14.3 接手团队应重跑的命令", 2)
    add_code(doc, """# 1) 建一个全新的测试目录，不复用 build/mingw_tests
cmake -S . -B build/handover_tests -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DPANTHERA_BUILD_TESTS=ON `
  -DCMAKE_TOOLCHAIN_FILE=D:/PanSoftware/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic `
  -DCMAKE_PREFIX_PATH=D:/PanSoftware/QT/6.2.0/mingw81_64
cmake --build build/handover_tests --parallel 4
ctest --test-dir build/handover_tests --output-on-failure --timeout 120

# 2) 单独排查当前失败用例
ctest --test-dir build/handover_tests -R therapy_imaging_algorithms -V
ctest --test-dir build/handover_tests -R planning_page_regression -V --timeout 120""")

    add_heading(doc, "15. 已知缺口、风险与技术债", 1)
    risks = [
        ["R-01", "P0", "安全", "真实硬件健康未完整接入 SafetyKernel，治疗联锁主要由仿真给出", "建立统一 DeviceHealthAggregator；双向心跳；失联默认安全；系统级故障注入测试"],
        ["R-02", "P0", "治疗输出", "TreatmentPage 只切换 SimulationDeviceFacade 输出，未驱动 RIGOL/Aigtek 真实闭环", "实现受控 PowerDeliveryService，加入互锁、反馈、硬件急停和实测能量积分"],
        ["R-03", "P0", "时序", "点执行固定450 ms推进，不按 dwellSeconds 实际驻留", "改为状态机：运动到位→稳定→开输出→驻留dwell→关输出→采证；使用单调时钟"],
        ["R-04", "P0", "产品定位", "README 明示非临床原型", "不得用于人体；建立 IEC 62304/ISO 14971 等适用过程并由质量/法规确认"],
        ["R-05", "P1", "版本", "0.1.0、V1.0、提交与未提交工作区不一致", "合并代码、清理工作树、打签名tag、生成SBOM与release notes"],
        ["R-06", "P1", "数据库", "database/enabled=false 仍退出，与部署文档冲突", "明确产品策略：要么支持离线只读演示，要么删除假开关并更新文档/测试"],
        ["R-07", "P1", "凭证", "root/123456 与 root/dobot 明文", "立即轮换；专用最小权限账号；Windows Credential Manager/加密站点配置"],
        ["R-08", "P1", "审批", "approvedBy 可能写死 physician，不是 user_account.id", "改用当前登录userId；数据库回填；外键测试"],
        ["R-09", "P1", "治疗水路", "TreatmentPage 流体卡片代码未接入UI", "决定是否删除或正式接入；不得保持半可用双实现"],
        ["R-10", "P1", "串口", "水泵/温控/液位共享COM3且客户端互斥关闭", "实现一个RS485总线调度器，串行队列、地址路由、超时/重试、独占管理"],
        ["R-11", "P1", "测试", "2项失败/超时，全量测试当前重新链接异常", "新目录清洁构建；修复用例；CI设超时、无头平台与硬件模拟"],
        ["R-12", "P1", "依赖", "UIM DLL 与工具链路径硬编码 D盘", "改为部署包相对路径/环境配置；校验DLL版本与哈希；许可归档"],
        ["R-13", "P1", "部署", "vcpkg applocal 无 objdump，无法验证动态依赖", "安装工具并用 Dependencies/objdump 验证；在干净机器冒烟"],
        ["R-14", "P1", "数据合规", "本地路径保存患者影像/证据，无静态加密与脱敏机制", "权限、加密、备份、留存、删除、导出审计和脱敏策略"],
        ["R-15", "P2", "文档", "README/部署文档仍描述旧的纯仿真/离线方案", "以本文档为过渡，随后同步正式架构、接口控制文件和操作SOP"],
        ["R-16", "P2", "算法", "体积/消融系数与呼吸补偿为原型估算，无外部传感器", "建立标定、数据集、金标准、误差预算与独立验证"],
        ["R-17", "P2", "审计", "部分数据管理审计 actor 使用 system；证据签名仅SHA-256", "统一绑定登录会话、医生、患者、设备；数字签名/可信时间"],
        ["R-18", "P2", "数据库迁移", "单一 CREATE TABLE IF NOT EXISTS 脚本，无schema版本和回滚", "引入顺序迁移、备份恢复演练、兼容策略"],
        ["R-19", "P2", "配置", "simulation/enabled、deployment device_mode 等没有统一控制实际对象", "建立 typed config 与启动诊断页；未知键/无效配置应失败或告警"],
        ["R-20", "P2", "资源", "application.setQuitOnLastWindowClosed(false)", "验证所有退出路径能停止相机、仿真、串口、TCP与线程；增加关闭集成测试"],
    ]
    add_table(doc, ["编号", "级别", "领域", "事实/风险", "建议措施"], risks, font_size=7.4, widths=[1.2, 1.0, 1.6, 6.0, 6.2])

    add_heading(doc, "16. 部署与现场配置步骤", 1)
    add_heading(doc, "16.1 建议交付包", 2)
    add_bullets(doc, [
        "Release 构建的 PanTheraConsole.exe；所有 Qt、MinGW/vcpkg 运行库；platforms/qwindows.dll；sqldrivers/qsqlmysql.dll；TLS/imageformat/style 插件。",
        "UISimCanFunc.dll 及其厂商版本/许可证/哈希；VISA runtime 安装说明；匹配的 MySQL client DLL。",
        "config/defaults.ini 的站点模板（不得使用默认口令）；db/schema 与带版本的迁移脚本。",
        "空 runtime/images、runtime/reports、runtime/evidence_documents，可写权限已配置。",
        "本说明书、硬件接线图、设备手册、串口地址表、Dobot固件版本、校准记录、验收记录。",
        "Git commit/tag、源代码归档、第三方依赖清单与二进制哈希。",
    ])
    add_heading(doc, "16.2 站点首次部署", 2)
    add_numbered(doc, [
        "安装/确认 Windows x64、显卡/显示器、USB/CAN/串口驱动、Dobot网络、VISA、MySQL 5.7兼容服务。",
        "创建 panthera_sys 和应用专用数据库账号；授予实际所需 DDL/DML 权限；执行初始化脚本；确认 utf8mb4。",
        "放置应用包并设置 runtime 三目录写权限；把站点配置与程序二进制分离备份。",
        "轮换数据库和SFTP口令；验证 qsqlmysql.dll + MySQL client DLL 架构一致。",
        "先断开所有治疗输出/执行器，用 --list-screens 验证三屏序号；设置显式屏幕索引。",
        "登录、患者CRUD、计划保存/重启持久化、报告/证据创建做软件冒烟。",
        "逐个设备单独接入：COM3总线、COM5、UIM、Dobot、RIGOL、USB摄像头；记录固件/序列号/端口。",
        "在假负载/无人体环境完成故障注入与联锁验证，之后才允许更高级别集成测试。",
    ])
    add_heading(doc, "16.3 运行前检查", 2)
    add_table(doc, ["检查项", "合格标准", "记录"], [
        ["版本", "exe、config、schema、UIM DLL、设备固件均有唯一版本/哈希", "____"],
        ["数据库", "专用账号可连接；备份已完成；无示例患者混入", "____"],
        ["三屏", "Dashboard/Planning/Treatment 映射正确；全屏退出路径可用", "____"],
        ["摄像头", "连续帧、8秒故障重启、重新枚举可恢复", "____"],
        ["COM3", "地址1/2/3/4无冲突；总线轮询稳定；拔线进入安全态", "____"],
        ["COM5/VISA", "功放与信号源保持输出关闭；命令/回读一致", "____"],
        ["UIM", "节点6/7/8、方向、限位、软/硬急停、回零、范围均实测", "____"],
        ["Dobot", "控制权、上电/使能、DI13、DO13/14、安全墙、安全原点、限速均实测", "____"],
        ["联锁", "任一水路/温度/功率/运动/急停/超声故障均阻止开始并在运行中中止", "当前不满足，整改后验收"],
        ["证据", "会话/点/报告/DOCX/SHA-256/医生/患者关联完整", "____"],
    ], font_size=8)

    add_heading(doc, "17. 接手团队建议的前四周计划", 1)
    add_table(doc, ["阶段", "必须完成", "退出标准"], [
        ["第1周：冻结与复现", "备份工作区；提交当前修改；新机清洁构建；固定Qt/MinGW或MSVC；复现13项测试", "可从tag重复构建；测试问题有工单；默认口令已轮换"],
        ["第2周：架构收口", "统一配置；决定MySQL强制策略；修approvedBy；RS485总线服务；移除/接入治疗流体卡片", "配置与文档一致；关键CRUD/登录/证据测试通过"],
        ["第3周：安全闭环", "真实设备健康汇聚；治疗输出服务；驻留状态机；失联/急停/限位默认安全", "所有P0故障注入测试通过；无仿真信号冒充真实健康"],
        ["第4周：实机与交付", "假负载整机联调；三屏/数据库/备份/恢复/证据验收；Release包与SBOM", "签字验收记录、已知问题清单、回滚方案、受控发布包齐全"],
    ], font_size=8)

    add_heading(doc, "附录 A：关键源码索引", 1)
    add_table(doc, ["主题", "文件"], [
        ["启动、数据库强制、三屏、摄像头", "apps/console/main.cpp"],
        ["构建版本与依赖", "CMakeLists.txt；CMakePresets.json；apps/console/CMakeLists.txt"],
        ["领域数据结构", "src/core/domain/system_types.h/.cpp"],
        ["共享上下文", "src/core/application/application_context.h/.cpp；event_bus.h/.cpp"],
        ["安全联锁", "src/core/safety/safety_kernel.h/.cpp"],
        ["患者/影像/计划/报告服务", "src/core/services/clinical_data_service.*；repositories/clinical_data_repository.h"],
        ["设备监控屏", "src/modules/dashboard/device_monitor_page.h/.cpp"],
        ["计划屏", "src/modules/planning/planning_page.h/.cpp；shared/therapy_imaging_algorithms.*"],
        ["治疗屏", "src/modules/treatment/treatment_page.h/.cpp"],
        ["数据管理", "src/modules/data/data_management_page.h/.cpp"],
        ["登录", "src/modules/auth/login_dialog.*；src/adapters/mysql/mysql_auth_repository.*"],
        ["MySQL", "src/adapters/mysql/mysql_clinical_data_repository.*；mysql_treatment_workflow_repository.*；mysql_repository_facade.*"],
        ["证据DOCX", "src/adapters/mysql/medical_evidence_document_writer.*"],
        ["Dobot", "src/adapters/dobot/*"],
        ["UIM", "src/adapters/uim/uim_motor_gateway.*"],
        ["水泵/温控/液位/功率", "src/adapters/waterpump/*；anthone/*；liquidlevel/*；aigtek/*；rigol/*"],
        ["摄像头", "src/modules/shared/usb_camera_frame_source.*"],
        ["配置", "config/defaults.ini；config/deployment.example.ini"],
        ["数据库结构", "db/schema/mysql_5_7_init.sql；docs/database_schema.md"],
        ["测试", "tests/CMakeLists.txt；tests/core；tests/adapters；tests/modules；tests/integration"],
    ], font_size=8)

    add_heading(doc, "附录 B：交接资料清单", 1)
    add_table(doc, ["资料", "当前状态", "接手动作"], [
        ["源代码与Git历史", "存在；工作区非干净", "提交、tag、远端备份"],
        ["数据库schema", "存在单一初始化SQL", "引入迁移版本与备份恢复SOP"],
        ["三屏UI说明", "本文档已补齐", "补现场截图/操作视频"],
        ["硬件协议", "源码可见", "补厂商手册、固件号、接线图、命令版本"],
        ["第三方二进制", "UIM/VISA/MySQL client等依赖外部环境", "归档合法副本、版本、许可证、SHA-256"],
        ["测试", "13目标；本次11/13已有二进制通过", "清洁构建并修到全绿；建立CI"],
        ["QMS文档", "docs/qms 有初稿", "与实际需求/风险/测试双向追踪"],
        ["临床/法规", "无可证明的验证结论", "由质量/法规负责人另行建立"],
        ["部署包", "本机Debug生成物", "产出受控Release并在干净机验证"],
        ["站点密钥", "仓库含默认弱口令", "移交渠道外单独安全分发"],
    ])

    add_heading(doc, "附录 C：术语", 1)
    add_table(doc, ["术语", "含义"], [
        ["Dashboard", "设备监控屏；兼具数据管理入口"],
        ["Planning", "影像采集、标注、重建、靶点与计划批准屏"],
        ["Treatment", "逐层逐点运动执行、进度与结果留证屏"],
        ["SafetyKernel", "治疗开始/暂停/继续/报警的统一软件联锁状态机"],
        ["SimulationDeviceFacade", "仿真设备数据与健康状态提供者；当前仍参与正式启动和治疗页"],
        ["UIM", "通过 UISimCanFunc.dll 访问的 USB-CAN 三轴电机系统"],
        ["Dobot Dashboard", "机械臂 TCP 文本命令接口；与运动命令socket并存"],
        ["VISA/SCPI", "仪器控制运行库与命令集；用于 RIGOL 信号源"],
        ["RS485/Modbus RTU", "水泵、温控、液位共用的串行总线/协议"],
        ["Evidence", "与医生、患者、会话、操作日志关联的可审计输出；当前含DOCX与SHA-256"],
    ])

    add_heading(doc, "附录 D：最终交接签字页", 1)
    add_paragraph(doc, "以下签字只确认资料/代码/设备状态已按清单交接，不替代产品安全验证或临床批准。")
    add_table(doc, ["角色", "姓名", "日期", "意见/未决事项"], [
        ["移交负责人", "", "", ""],
        ["接收研发负责人", "", "", ""],
        ["测试负责人", "", "", ""],
        ["硬件负责人", "", "", ""],
        ["质量/法规负责人", "", "", ""],
        ["项目负责人", "", "", ""],
    ], font_size=9)
    add_paragraph(doc, f"文档结束。生成依据：{ROOT}，快照日期 {SNAPSHOT_DATE}。")

    doc.save(OUT_PATH)
    print(str(OUT_PATH))


if __name__ == "__main__":
    main()
