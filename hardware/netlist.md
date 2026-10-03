# 网表（Netlist）— 与固件引脚定义严格一致

> 来源：`src/config.h` 的 `CONFIG_IDF_TARGET_ESP32S3` 分支（commit `6a3f78b` 后）。
> 画原理图时以本表为准；改固件引脚必须同步改本表。

## 电源网

| 网络 | 连接 |
|------|------|
| `+5V` | J1(供电) → U1(VIN/5V)、U3(VIN)、F1→(BAT+ 分压支路取 BAT+，见下) |
| `+3V3` | U1(3V3 输出，板载 LDO) → U2(VCC)、U4(VDD)、SW1 上端 |
| `GND` | U1、U2、U3、U4、LS1(负端经 U3)、SW1 无（见信号）、R2、C1–C6、J1 |
| `BAT+` | 电池正 → F1 → 分压支路 + 5V 升压/稳压输入 |

## 信号网

| 网络 | 起点（ESP32-S3） | 终点 |
|------|------------------|------|
| `I2S0_SCK`  | U1 GPIO15 | U2 SCK |
| `I2S0_WS`   | U1 GPIO16 | U2 WS |
| `I2S0_SD`   | U1 GPIO17 | U2 SD |
| `I2S1_BCLK` | U1 GPIO4  | U3 BCLK |
| `I2S1_LRC`  | U1 GPIO5  | U3 LRC |
| `I2S1_DIN`  | U1 GPIO6  | U3 DIN |
| `I2C_SDA`   | U1 GPIO8  | U4 SDA |
| `I2C_SCL`   | U1 GPIO9  | U4 SCL |
| `PTT`       | U1 GPIO1  | SW1 下端（SW1 上端=+3V3；并联 C 100nF→GND 可选去抖） |
| `ADC_BAT`   | U1 GPIO10 | R1–R2 中点；C1 从中点到 GND |
| `SPK_OUT+`  | U3 OUT+   | LS1 正 |
| `SPK_OUT-`  | U3 OUT-   | LS1 负 |
| `MIC_LR`    | U2 L/R    | GND（固定左声道） |
| `AMP_GAIN`  | U3 GAIN   | 悬空=15dB；下拉电阻可降增益（9/12dB，查 MAX98357 表） |
| `AMP_SHDN`  | U1 GPIO7（预留，固件暂未启用） | U3 SD（做静音功能时接） |

## 去耦位置要求

- C2/C3/C4：各 IC 的 VCC 引脚 2mm 内。
- C5：U3 VIN 就近（发声电流尖峰主要靠它）。
- C6：U1 3V3 输出脚。
- 星型接地：U2 与 U3 的 GND 分别走线，单点汇到 U1 GND。

## 嘉立创 EDA 使用提示

- BOM 元件在立创商城按名称搜索即可（INMP441、MAX98357、SSD1306 0.96、ESP32-S3-WROOM-1-N16R8 均有封装）。
- 模块类（U2/U3/U4）建议画成接插件（排母）而非焊死模块，便于替换。
- 先按本表画原理图 → 编译网表核对 → 再布 PCB（规则见 `pcb-guidelines.md`）。
