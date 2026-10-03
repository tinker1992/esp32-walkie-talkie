# hardware/ — 原理图/PCB 资料包

供后续在**嘉立创 EDA（lceda.cn / EasyEDA Pro）**中绘制原理图与 PCB 的 agent 使用。

## 文件说明

| 文件 | 用途 |
|------|------|
| `BOM.csv` | 元件清单（位号/型号/封装/数量/备注），可直接导入立创商城匹配器件 |
| `netlist.md` | 网表：电源网 + 信号网 + 去耦要求，**画原理图的连线依据** |
| `pinmap.json` | 固件引脚分配的唯一事实来源（含两块板的身份、禁区、空中格式） |
| `pcb-guidelines.md` | 布局/接地/去耦/天线净空等 PCB 规则 |

## 与其他文档的关系

- 可视化接线图：`../docs/wiring.mermaid`（本目录的图形版）
- 架构与数据流：`../docs/architecture.mermaid`
- 上机验证：`../docs/bringup-checklist.md`
- 踩坑必读：`../docs/AGENT_HANDOVER.md`

## 给接手的 agent：推荐工作流

1. 先读 `docs/AGENT_HANDOVER.md`（尤其 B/C/D 三节，避免重复踩坑）。
2. 在嘉立创 EDA 专业版新建工程 → 按 `netlist.md` 放置器件、连线成原理图；
   器件用 `BOM.csv` 名称在立创商城检索（模块类建议画成排母插座）。
3. 网表核对：原理图 DRC/网络表与本文件逐网比对，**任何不一致以 `src/config.h` 为准**（它是固件真正读的）。
4. 按 `pcb-guidelines.md` 布局布线；两板设计完全相同（板2 的 OLED 插座可不贴）。
5. 若改动了任何 GPIO 分配：同步修改 `src/config.h`、`pinmap.json`、`netlist.md`、`docs/wiring.mermaid` 四处，
   并重新烧录**两台**板子。

## 关键硬约束（别画错）

- PTT：GPIO1 → 按键 → **3V3**（高电平有效，不是接地）。
- INMP441：L/R 接 GND（左声道）。
- MAX98357：VIN 走 **5V**（不要用 3V3 推功放）；SD 引脚预留接 GPIO7（静音功能，固件待启用）。
- 电池分压：BAT+ → 100k → GPIO10 → 100k → GND，并 100nF 对地；BAT+ 先过 500mA PTC。
- 禁区：GPIO26–32（flash）、GPIO33–37（八线 PSRAM）、GPIO19/20（原生 USB）、GPIO0/3/45/46（strapping）。
