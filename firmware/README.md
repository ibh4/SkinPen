# SkinPen V0.2 —— PANTONE SkinTone 皮肤色号光谱采集笔

SkinPen 是一台基于 ESP32-S3 的手持式皮肤色号检测笔：对准皮肤按下测量键，
AS7341 十通道光谱传感器采集 10 帧光谱数据（F1~F8 / CLEAR / NIR），
自动计算 mean / std / CV 并判定数据质量，结果**同时**保存到 TF 卡（TXT，永不覆盖）
并通过 USB CDC 实时输出 JSONL 供 Mac 端接收脚本显示与双备份。
配套 139 条 PANTONE SkinTone 色卡库，用于逐张校准采集与后续粉底/口红推荐建模。

---

## 1. 硬件清单与接线

| 部件 | 型号 / 说明 | 接线 |
|---|---|---|
| 主控板 | Waveshare ESP32-S3-Touch-LCD-1.47（JD9853 屏 172x320，AXS5106 触摸） | — |
| 光谱传感器 | AS7341-V2（I2C 地址 0x39） | SCL→**GPIO1**，SDA→**GPIO2**，VIN→3V3，GND 共地 |
| 存储 | TF 卡（FAT32，板载 SDMMC 4 线） | 板载卡槽 |
| 上位机 | Mac / PC，USB Type-C 直连 | 板载 USB 口（USB Serial/JTAG 控制台） |
| 测量键 | 板载按键 GPIO7 | 短按=开始采集，长按=取消 |

I2C 总线分配：**端口 0**（GPIO41/42）→ 触摸 AXS5106；**端口 1**（GPIO1/2）→ AS7341。
板上无 RTC，时间戳以 `2026-01-01 00:00:00` 为开机基线推进（Mac 端可按接收时刻覆盖）。

### 1.1 主控板完整引脚定义（提取自 components/esp_bsp 头文件）

**显示屏 JD9853（SPI）** — `bsp_display.h`

| 信号 | GPIO |
|---|---|
| SPI MOSI | 39 |
| SPI SCLK | 38 |
| LCD CS | 21 |
| LCD DC | 45 |
| LCD RST | 40 |
| 背光 BL | 46 |

**触摸 AXS5106（I2C 地址 0x63）+ IMU QMI8658（共用总线）** — `bsp_i2c.h` / `bsp_touch.h`

| 信号 | GPIO |
|---|---|
| I2C0 SDA | 42 |
| I2C0 SCL | 41 |
| 触摸 INT | 47 |
| 触摸 RST | 48 |

**TF 卡（SDMMC 4 线）** — `bsp_sdcard.h`

| CLK | CMD | D0 | D1 | D2 | D3 |
|---|---|---|---|---|---|
| 16 | 15 | 17 | 18 | 13 | 14 |

**按键 / 电源 / 外接**

| 功能 | GPIO | 说明 |
|---|---|---|
| USER 键 | 7 | 本项目测量键：短按=采集，长按=取消（低电平有效，内部上拉） |
| BOOT 键 | 0 | 下载模式（固件卡死无法烧录时按住 BOOT 再按 RST） |
| 电池 ADC | 12 | ADC2_CH1 |
| AS7341 SCL | 1 | 外接传感器，I2C 端口 1 |
| AS7341 SDA | 2 | 外接传感器，I2C 端口 1 |

USB 走原生 USB-Serial/JTAG（GPIO19/20 固定），JSONL 即从该口输出。

## 2. 新电脑接管：环境搭建

1. 安装 **ESP-IDF v5.5**（≥5.3.1 亦可），激活：`. ~/esp/esp-idf/export.sh`（本机路径 `/Users/pwngwc/projects/ESP32/esp-idf-v5.5`）。
2. 进入工程目录后直接 `idf.py build`。依赖组件（lvgl 8.4 / esp_lvgl_port / esp_lcd_touch / button / esp_bsp / jd9853 / axs5106）已包含在 `components/` 与 `managed_components/` 中，离线可编译。
3. 烧录：`idf.py -p /dev/cu.usbmodemXXXX flash monitor`（Mac 上设备名形如 `/dev/cu.usbmodem21101`）。

TF 卡准备（**必须先做，否则色卡加载失败**）：
- 卡根目录建 `config/` 文件夹，拷入 `tools/pantone_skintone.csv`（139 条色卡，UTF-8 带 BOM 已兼容，字段 `seq,code,name_en,enabled`）。
- 数据将写入卡上 `/data/YYYYMMDD/HHMMSS_seq_code.txt`。

## 3. 软件架构（main/ 目录）

| 文件 | 职责 |
|---|---|
| `main.c` | 系统初始化：JD9853 显示（**必须 `esp_lcd_panel_set_gap(panel,34,0)`**，否则 UI 左移+右侧花屏）、上电白屏清 GRAM 残留、AXS5106 触摸、GPIO7 测量键、各模块自检（LCD/LVGL/TF/Pantone/AS7341 串口输出，错误在 GUI 显示不 panic）、JSONL hello 上报 |
| `as7341.c` | AS7341 驱动：独立 I2C1（GPIO1/2、0x39、100kHz）、SMUX 双表（与 Adafruit_AS7341 官方一致）、ATIME=29 / ASTEP=599 / GAIN=16X、读帧失败软恢复重试；**传感器未接时自动进入 SIM 模式**输出模拟数据跑通全链路 |
| `pantone.c` | TF 卡读 `/config/pantone_skintone.csv`：跳 BOM、按 seq 升序排序、查重复 seq/code，加载 139 条 |
| `sd_manager.c` | SDMMC 挂载 `/sdcard`；数据 TXT 保存 `/data/YYYYMMDD/HHMMSS_seq_code.txt`，**同名自动加 `_1/_2` 后缀不覆盖** |
| `measurement.c` | 独立采集任务：锁卡 → LED 8mA 亮 30ms → 10 次读帧 → mean/std/CV → 存 TXT → 发 JSONL → 自动切下一张；支持取消 |
| `gui.c` | LVGL 8.4 浅色界面（172x320）：彩色启动画面 → 主页 / 色卡选择 / 采集进度 / 结果四页 |
| `font_zh_16.c` | 自制 16px 中文字体（Hiragino Sans GB W6 提取，仅含所需 13 汉字，`tools` 可重生成） |

关键 sdkconfig（`sdkconfig.defaults`）：`CONFIG_FATFS_LFN_HEAP=y`（**长文件名必须，否则 fopen 全失败**）、`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`（JSONL 走 USB CDC）、16MB Flash 自定义分区表、PSRAM OCT 80M、`CONFIG_LV_COLOR_16_SWAP=y`。

## 4. 采集流程与数据格式

单次测量（点 MEASURE 或 GPIO7 短按）：
锁定当前色卡 → LED（8mA）亮 30ms → 连续 10 帧读光谱 → 计算 mean/std/CV → 写 TXT → 输出 JSONL `saved` → 自动跳下一张。长按 GPIO7 或界面取消键中止。

TXT 格式（`/data/日期/时间_seq_code.txt`）：
```
# SKINPEN_RECORD_V1
timestamp=...  session_id=...  device_id=SKINPEN_V01  firmware=0.2.0
sensor=AS7341-V2|SIM-DEMO  card_seq=..  card_code=..  card_name_en=..
gain=16X  atime=29  astep=599  led_state=ON  led_ma=8  repeat_count=10
columns=repeat,f1,f2,f3,f4,f5,f6,f7,f8,clear,nir
1,..,..,..（10 行原始帧）
summary_mean=...  summary_std=...  summary_cv=...
```

USB JSONL 协议（以 `{` 开头的行）：
```json
{"type":"hello","device":"SkinPen V0.2","firmware":"0.2.0","sim":false}
{"type":"start","seq":1,"code":"1R00SP","timestamp":"..."}
{"type":"sample","seq":1,"code":"1R00SP","repeat":1,"ch":[...10 通道...]}
{"type":"saved","seq":1,"code":"1R00SP","file":"/data/.../xxx.txt","status":"OK"}
{"type":"cancelled","seq":1,"code":"1R00SP"}
```

## 5. Mac 端接收脚本（tools/skinpen_receiver.py）

```bash
python3 tools/skinpen_receiver.py                # 自动探测 /dev/cu.usbmodem*
python3 tools/skinpen_receiver.py -p /dev/cu.usbmodem21101
```
实时表格显示、JSONL + TXT 双备份、CV 超限 / 饱和 / seq 缺失重复 QC 告警；统计在本机复算，与固件一致（防串口丢行）。

其他工具：`make_pantone_csv.py`（重新生成色卡 CSV）；中文字体可用 lv_font_conv 以 `--symbols "皮肤色号检测笔粉底口红推荐"` 重生成 `font_zh_16.c`。

## 6. GUI 说明

- 启动画面（约 1.5s 后淡入主页）：白底三行——`Skin`(玫红)`Pen`(亮蓝) / `皮肤色号检测笔`(灰) / `粉底口红推荐`(六字六色)。
  **坑**：LVGL recolor 语法必须 `#RRGGBB 文字#`——6 位十六进制后跟**空格**，否则文字被当命令参数吞掉显示方块。
- 主页：当前色卡 seq/code/name、设备状态、进度；LIST 单独一行，上/下箭头在 MEASURE 之后。
- 规范：仅 14/16px 字体、label 固定宽 + WRAP、屏幕 padding=5 安全区、错误底部状态栏红字不弹窗。
- 亮度 80% 可调（`bsp_display_set_brightness`）。

## 7. 当前状态与待办

- [x] 固件编译通过（app 约 670KB / 6MB 分区）并已烧录验证；SIM 模式全链路（CSV→GUI→TXT→JSONL→Mac 接收）跑通。
- [x] 139 条色卡加载、同名文件 `_1/_2` 防覆盖、启动画面彩色三行。
- [ ] **待办（硬件）**：AS7341 WHOAMI 读取失败（I2C GPIO1/2 无 ACK，固件自动回落 SIM 模式）。排查：V1/V2 VIN→3V3、GND 共地、SCL→GPIO1、SDA→GPIO2、接线牢固后复位，串口日志 `AS7341 探测成功` 即正常。
- [ ] 真机接传感器后用实测光谱复验 CV 阈值与 LED 电流。
- [ ] 后续方向：基于 139 条色卡光谱特征做粉底/口红色号推荐模型（数据已按 PANTONE SkinTone 体系组织）。

## 8. 踩坑速查

| 症状 | 原因 / 解决 |
|---|---|
| UI 左移 + 右侧花屏 | JD9853 GRAM 240x320，必须 `set_gap(panel, 34, 0)` |
| TF 卡上长文件名 fopen 失败 | 未开 `CONFIG_FATFS_LFN_HEAP=y` |
| 中文显示方块 | 字体缺字或 recolor 缺空格（见第 6 节） |
| 上电花屏/黑屏一闪 | GRAM 残留；main.c 已整屏填白 0xFFFF 清除 |
| 串口收不到 JSONL | 控制台必须是 USB Serial/JTAG；Mac 端设备 `/dev/cu.usbmodem*` |
| AS7341 无 ACK | 接线/供电/共地问题，固件自动进 SIM 模式不阻塞 |
