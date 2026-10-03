# Agent 交接手册（踩坑汇总）

> 面向接手本项目的 AI agent / 开发者。全部为**实际踩过并验证**的坑，按类别整理。
> 配套阅读：`docs/architecture.mermaid`（架构）、`docs/wiring.mermaid`（接线）、
> `docs/bringup-checklist.md`（上机自检）、`hardware/`（原理图/PCB 资料包）。

## A. 工具链与构建

1. **espressif32@3.4.0 不支持 ESP32-S3**（Arduino core 1.0.x/IDF 3.x）。S3 需要 `platform = espressif32@^6`
   （本地用 6.12.0 + Arduino core 2.0.17，离线可编）。PlatformIO 的 `platform` 是**按 env 生效**的，
   旧 env（tinypico/lolin32）保持 3.4.0 不动即可，不要因为升级平台破坏旧构建目标。
2. **`pio` CLI 不在 PATH**：位于 `~/.platformio/penv/Scripts/pio.exe`，先 `export PATH` 再加。
3. **`pio device monitor` 无法在后台/管道下运行**（需要交互终端，直接 exit 1）。抓串口日志用
   `scripts/serial_capture.py`（penv 自带 pyserial）。注意：**打开串口会复位板子**，正好能抓到完整 boot 日志。
4. **大包下载经常卡死**（u8g2、micro-opus 多次超时，残留的 pio.exe 还会锁住 `.pio/libdeps` 导致后续安装全部挂起）。
   对策：优先选**无外部依赖**的实现（本项目 OLED 驱动即手写）；遇到安装卡死先
   `taskkill //F //IM pio.exe` 清僵尸再重试。
5. **Opus 库选型**：`esphome/micro-opus` 是纯 ESP-IDF 组件（`frameworks: espidf`，无 Arduino SConscript），
   在 Arduino 框架下 PlatformIO 不会编译其 C 源 → 链接失败。用 **`sh123/esp32_opus`**（标准 libopus C API：
   `opus_encoder_create/opus_encode/opus_decode/opus_encoder_ctl`，16kHz 单声道 20ms 帧）。

## B. 代码结构与约定（本项目最重要的三条）

6. **`lib/` 内严禁 `#include "config.h"`**：PlatformIO LDF 会把每个依赖库的 `src/` 加入包含路径，
   esp32_opus 自带同名 `config.h`，会**静默顶掉**项目的 `src/config.h`（表现为宏全部未定义，但编译照过）。
   正确姿势：库模块保持无配置依赖，参数由 `src/` 侧注入——见 `WalkieDisplay::begin(sda,scl,addr,col_offset)`
   和 `EspNowTransport::set_lmk()`。
7. **`src/config.h` 第一行必须 `#include <sdkconfig.h>`**：引脚按 `CONFIG_IDF_TARGET_ESP32S3` 分支选择，
   某些编译单元里该宏未定义会**静默走错分支**（用了经典 ESP32 的引脚还编译通过）。
8. **新功能一律用 build_flags 宏门控**（`USE_OPUS_CODEC` / `USE_OLED_DISPLAY`），只在 `[env:esp32s3]` 开启，
   旧 env 走 `#else` 原路径。每次改动**必须双 env 都编译通过**再提交：
   `pio run -e esp32s3 -e lolin32`。

## C. ESP-NOW / 无线

9. **ESP-NOW 不支持对广播（multicast）peer 加密**——本项目最大的坑：给 `FF:FF:FF:FF:FF:FF` 设
   `encrypt=true` 会让 `esp_now_add_peer` 返回 `ESP_ERR_ESPNOW_ARG`，peer 根本没注册上，
   之后所有 `esp_now_send` 报 `ESP_ERR_ESPNOW_NOT_FOUND`，整机变哑。
   LMK 加密只对**单播 peer** 有效。要真加密：① MAC 发现+单播配对，或 ② 应用层 AES-GCM（推荐，与传输无关）。
10. **当前是明文广播**（`config.h` 里 `USE_ESP_NOW_LMK` 保留但 `begin()` 对广播跳过加密并打印
    `ESP-NOW: LMK ignored...`，属已知现状，不是 bug）。
11. **判断链路是否通的唯一日志**：接收端每 25 个有效包打印 `[RX] valid packets=N len=M`。
    发送端异常表现为 `Failed to send: ...`。正常对讲：一台按 PTT，另一台应见 `[RX]` 递增。
12. **空中格式变更必须两台同版本烧录**（例：Opus 包前置 1 字节 seq）。版本错开的症状=全是噪声。

## D. 硬件与板子

13. **PTT 高电平有效**：固件 `INPUT_PULLDOWN` + 读到 HIGH 才发射 → 按键接 **GPIO1→3V3**（不是 GND！）。
14. **INMP441 的 L/R 必须接 GND**（左声道，配 `I2S_MIC_CHANNEL=ONLY_LEFT`）；悬空=无声或大噪声。
15. **OLED 列偏移**：真 SSD1306 `OLED_COL_OFFSET=0`；CH1116 克隆（132 列 RAM、窗口偏移 2）需 `=2`。
    症状区分：偏移错 → 一侧边缘出现擦不掉的杂点列。**本项目实测板载屏为真 SSD1306，值=0。**
16. **两块板的身份**（COM 号会变，用 CH343 序列号认）：
    板1（带 OLED）`SER=5C93065295`，MAC `EC:DA:3B:4D:56:34`；板2（无 OLED）`SER=5C93083344`，MAC `CC:BA:97:0C:38:C4`。
17. **板2 USB 枚举间歇失败**（同线同口板1永远正常）= 板子自身 Type-C 口/供电问题。
    对策：翻转 Type-C 插头 180°、或改用板上**原生 USB 口**（`USB JTAG/serial debug unit`，同样能烧录）。
18. **S3 引脚禁区**（N16R8）：GPIO26–32（flash）、GPIO33–37（八线 PSRAM）、GPIO19/20（原生 USB）、
    GPIO0/3/45/46（strapping）。当前占用与空闲清单见 `hardware/pinmap.json`。
19. **杂音分层定位**：断续咔哒=丢包/欠载（软件已做 PLC+2帧预缓冲+DC隔直）；持续嘶声/嗡嗡=模拟侧
    （优先换独立供电→功放 VIN 加大电解→星型接地→GAIN 降档→线短）。
20. **电池分压**：`Rtop=Rbot=100k`（1:1，对应 `BATT_DIVIDER=2.0`）+ ADC 脚 100nF；必须走 **ADC1**（GPIO1–10）。
    未接电池时 ADC 悬空 → 屏上 `bat` 乱跳属正常。

## E. Git / 远端

21. **origin 走 SSH**：`git@github.com:tinker1992/esp32-walkie-talkie.git`。HTTPS 的 403 是
    GCM 缓存坏 token 所致（非权限问题），不要试图用 HTTPS 推。测试：`ssh -T git@github.com`。
22. 提交风格：一行祈使句主题 + 解释"为什么"的正文；一个逻辑步骤一个 commit；
    硬件/固件行为变更要在正文写明（尤其空中格式）。

## 接手后的快速自检命令

```bash
export PATH="$HOME/.platformio/penv/Scripts:$PATH"
pio run -e esp32s3 -e lolin32            # 双目标必须全绿
pio device list                           # 用 SER 认板
pio run -e esp32s3 -t upload --upload-port COMx
python scripts/serial_capture.py COMx 30   # 后台抓 30 秒串口日志（会复位板子，含 boot 日志）
```
