# ESP32-S3 对讲机 · 上机自检清单 (Bring-up Checklist)

> 适用固件：S3 迁移 + Opus + ESP-NOW LMK + SSD1306 OLED（commit `64b8c06` 及之后）。
> 用法：按顺序过，**前一步不通过就别往下走**。每步给"现象 → 排查 → 处理"。
> 编译已通过，但本清单是**首次上硬件**用的，很多问题只有上电才暴露。

---

## 0. 工具链 / 烧录

- [ ] 烧 S3 目标：`pio run -e esp32s3 -t upload -t monitor`（波特率 115200）。
- [ ] 烧不到 / 端口不认：
  - DevKitC-1 通常有 2 个 USB（一个走串口芯片、一个是 S3 原生 USB）；换一个口试。
  - 按住 **BOOT** 键再上电进入下载模式；或指定 `upload_port` / `upload_speed 921600`。
- [ ] 启动有 **PSRAM 初始化 panic / 崩溃**：说明模组不是 N16R8（八线 PSRAM）。
  - 四线 PSRAM（如 N16R2）：`platformio.ini` 把 `board_build.arduino.memory_type = qio_opi` 改成 `qio_qspi`。
  - 无 PSRAM：删掉 `-D BOARD_HAS_PSRAM` 和 memory_type 行。
- [ ] 启动有 **Brownout detector was triggered**：供电不足（见第 1 步）。

## 1. 供电

- [ ] 用 ≥500mA 的 5V 源（电脑 USB 口或充电头），别用瘦弱的 hub。S3 + I2S 功放发声瞬间电流尖峰大。
- [ ] 3.3V 由各芯片 LDO/板载稳压；**MAX98357 和 ESP32-S3 共地**。
- [ ] 每个模块 VCC-GND 就近加 100nF + 10µF 去耦（喇叭无声/底噪/随机重启常常是供电/去耦）。

## 2. 看串口启动日志（正常应出现）

```
Application started
My IDF Version is: v4.4.x
My MAC Address is: A4:..:..:..:..:..        (或你的 MAC)
ESPNow Init Success
ESP-NOW: LMK link encryption enabled         ← LMK 生效
Opus: ready @ 16000 Hz, frame 320 samples, 24000 bps   ← Opus 生效
Started Receiving
```
- [ ] 少了 `LMK ... enabled`：`config.h` 里 `USE_ESP_NOW_LMK` 被注释了。
- [ ] 少了 `Opus: ready`：不是 S3 环境或 `USE_OPUS_CODEC` 没开（会退回 8-bit PCM）。
- [ ] 反复 `Opus codec failed to initialise` / `Opus: allocation failed`：堆内存不足，去掉其它占用或降 Opus 参数。

## 3. OLED（SSD1306 0.96" I2C）

接线：`SDA=GPIO8`、`SCL=GPIO9`、`VCC=3.3V`、`GND=共地`。

- [ ] 先跑 **I2C 扫描**确认地址（多数 0x3C，个别 0x3D）。扫描不行就基本是接线问题。
- [ ] **完全黑屏**：90% 是 **SCL/SDA 接反** 或没共地 / 没供 3.3V。先把 SDA、SCL 对调试一次。
- [ ] 地址是 0x3D：改 `config.h` `#define OLED_I2C_ADDR 0x3D`。
- [ ] 对调+地址都对仍全黑，但你这块是**便宜"0.96 OLED"的 CH1116 兼容芯片**（外观与 SSD1306 一样）：我的驱动是标准 SSD1306 初始化，CH1116 需要不同的初始化序列（部分 CH1116 能用 SSD1306 命令，部分不行）。确认芯片型号；若确定 CH1116，告诉我，我加一个 CH1116 初始化分支。
- [ ] 正常：开机先显示 `S3 WALKIE` 自检约 3 秒，然后进入**单页仪表盘**（模式/通话计时、电平条、电量、AES、编码）。

## 4.（已移除）旋转编码器

试验阶段已去掉编码器，OLED 只保留一页仪表盘，无需翻页。**GPIO12/13/14 已释放**，可留空或另作他用。

## 5. PTT 发射键 ⚠️（纠正一处早前的接线说明）

固件：`pinMode(GPIO_TRANSMIT_BUTTON, INPUT_PULLDOWN)` + `digitalRead()==HIGH 才发射` → **高电平有效**。

- [ ] 正确接法：按键一端接 **GPIO1**，另一端接 **3V3**（不是接 GND！）。
  - 我最早 S3 迁移说明里写的"按下接地"是**错的**，特此更正。
- [ ] 快速验证：不接喇叭，按住键看串口是否打印 `Started transmitting`；松开打印 `Finished transmitting`。没打印=键没读到高电平（接线/上拉）。
- [ ] 若你更想"按键接 GND 触发"：改成 `INPUT_PULLUP` 且判断 `digitalRead()==LOW` 即可（需要我改再说）。

## 6. 麦克风 INMP441（发射链路）

接线：`SCK=GPIO15`、`WS=GPIO16`、`SD=GPIO17`、`L/R→GND`（选左声道，匹配 `I2S_MIC_CHANNEL=...ONLY_LEFT`）、`VCC=3.3V`。

- [ ] 发射时 RX 页/STATUS 无关系；看**对端**或本机 TX 页里的 `MIC` VU 条是否随说话跳动。
- [ ] VU 一直为 0：多半是 `L/R` 声道设置与实际不符 → 把 `config.h` 的 `I2S_MIC_CHANNEL` 换成 `I2S_CHANNEL_FMT_ONLY_RIGHT`（或把 L/R 接到 3V3）后重烧。也可能是 SCK/WS/SD 三根里接错。
- [ ] 噪声/电流声大：见第 1 步去耦；麦克风远离喇叭与功放走线；INMP441 的 SD 别悬空。

## 7. 喇叭 MAX98357（接收链路）

接线：`BCLK=GPIO4`、`LRC=GPIO5`、`DIN=GPIO6`、`+/-接喇叭`、`VCC=5V 或 3.3V`、`GND 共地`。`GAIN/SD` 脚按你板子说明接（本固件未用 SD，已设 -1）。

- [ ] 需要**两台**同固件互相收发，或一台发一台收。
- [ ] 完全无声：查 BCLK/LRC/DIN 三根是否对上、功放供电、喇叭线；确认在 RX 状态且对端确实在发。
- [ ] 声音小/爆：调 MAX98357 的 `GAIN` 脚组合；或调 Opus 码率。
- [ ] 卡顿/咔哒：解码欠载 → 看第 8 步。

## 8. Opus 音质 / 卡顿

- [ ] 两台都必须**同为 Opus 版固件**（都从 `esp32s3` 环境烧）。Opus 版与 8-bit 版**不能互通**（空中格式不同）。
- [ ] 断音/丢字：帧太大或抖动缓冲不足 → 试 `OPUS_FRAME_MS` 20、`OPUS_BITRATE` 提到 32000。
- [ ] 距离近但杂音严重：ESP-NOW 信道干扰，换 `ESP_NOW_WIFI_CHANNEL`（两台一起换）。
- [ ] 解码偶发崩溃：应用任务栈已 24KB；仍崩就把 `OPUS_COMPLEXITY` 调低（如 3）。

## 9. ESP-NOW LMK 配对（加密）

- [ ] 两台 `ESP_NOW_LMK` 的 16 字节**完全一致**，且 `ESP_NOW_WIFI_CHANNEL` 一致；否则互相听不懂。
- [ ] 串口有 `ESP-NOW: LMK link encryption enabled` 与 `ESPNow Init Success`。
- [ ] 先近距离测通，再拉远。
- [ ] 安全边界（心里有数）：LMK 是**固件里预共享**的链路层加密，能防空中明文抓取；但拿到实物仍可提取密钥。要更强，下一步做**应用层 AES-GCM + 计数器防重放**，并把密钥烧进 **eFuse/Flash 加密**。

## 10. 电池电压 ADC

接线：电池 `+` 经**电阻分压**接到 `GPIO10`（本固件按 `BATT_DIVIDER 2.0` ≈ 1:1 分压，即 ADC 脚看到约电池一半）。

- [ ] 安全红线：ADC 脚电压**不得超过 3.3V**。4.2V 锂电 1:1 分压 ≈ 2.1V，安全；确认你的分压比。
- [ ] 屏上 `bat` 百分比明显不对/恒 0 或恒 100：
  1. 万用表量 GPIO10 实际电压，和固件读到的 `analogReadMilliVolts` 比对（可在 `ui_service` 里临时 `Serial.println(mv)`）；
  2. 用真实比值改 `BATT_DIVIDER`；
  3. 按电芯类型改 `BATT_MV_EMPTY`/`BATT_MV_FULL`（LiPo 常用 3300/4200；带充电保护/磷酸铁锂不同）。
- [ ] 没电池（纯 USB）：把 STATUS/RX 的电量条去掉（或 `BATT_ADC_PIN` 悬空时会读到乱值——那就删除显示行）。

## 11. 综合联调（都单独通过后）

- [ ] 两台：A 按住 PTT 说话 → B 听到且 B 屏进 TX? 不，是 **B 在 RX 页、有 `in` 电平条**；B 按住回传同理。
- [ ] 连续发射 30s+ 看有无重启/堆耗尽（串口 `heap_caps_print_heap_info` 或 `esp_get_free_heap_size()` 打印）。
- [ ] 拉开距离测范围与丢包；换信道对比。
- [ ] 编码器、OLED、电量长时间稳定不花屏/不卡。

---

## 已知"还没做"（非 bug）
- 屏上未显示真实 RSSI 数值与在线台数（需把 ESP-NOW 收包回调升级到 `esp_now_recv_info_t` 拿 rssi）——要做告诉我。
- 全为编译验证，未上机；上机若个别字形/布局要微调很常见。
- 架构图：`docs/architecture.mermaid`（尚未纳入 git）。
