# SkinPen V0.5 移植指南（接线 + 代码逻辑）

> 生成日期: 2026-10-12。本文档面向**迁移到另一块 ESP32 硬件（可能改接引脚）**的场景，
> 覆盖全部硬件接线、每个引脚在代码中的确切位置、软件架构、数据流与迁移 checklist。

---

## 1. 硬件平台与接线

原平台: **微雪 ESP32-S3-Touch-LCD-1.47**（ESP32-S3R8, 16MB Flash, 8MB Octal PSRAM）
屏幕: JD9853 172×320 竖屏，固件横屏使用 320×172（USB 口在右侧）
触摸: AXS5106 电容触摸（与 AS7341 探头共用同一条 I2C0 总线）

### 1.1 板上外设引脚（固件默认值）

| 功能 | GPIO | 代码位置 |
|---|---|---|
| I2C0 SDA（触摸 + AS7341 共用） | **42** | components/esp_bsp/bsp_i2c.h:7 |
| I2C0 SCL（触摸 + AS7341 共用） | **41** | components/esp_bsp/bsp_i2c.h:8 |
| 触摸 INT | 47 | components/esp_bsp/bsp_touch.h:8 |
| 触摸 RST | 48 | components/esp_bsp/bsp_touch.h:9 |
| LCD MOSI | 39 | components/esp_bsp/bsp_display.h:15 |
| LCD SCLK | 38 | components/esp_bsp/bsp_display.h:16 |
| LCD CS | 21 | components/esp_bsp/bsp_display.h:17 |
| LCD DC | 45 | components/esp_bsp/bsp_display.h:18 |
| LCD RST | 40 | components/esp_bsp/bsp_display.h:19 |
| LCD 背光 | 46 | components/esp_bsp/bsp_display.h:20 |
| LCD MISO | NC（未接） | components/esp_bsp/bsp_display.h:14 |
| TF 卡 CLK | 16 | components/esp_bsp/bsp_sdcard.h:9 |
| TF 卡 CMD | 15 | components/esp_bsp/bsp_sdcard.h:10 |
| TF 卡 D0 | 17 | components/esp_bsp/bsp_sdcard.h:11 |
| TF 卡 D1 | 18 | components/esp_bsp/bsp_sdcard.h:12 |
| TF 卡 D2 | 13 | components/esp_bsp/bsp_sdcard.h:13 |
| TF 卡 D3 | 14 | components/esp_bsp/bsp_sdcard.h:14 |
| 测量按键（短按=开始测色，长按=取消） | 7 | main/main.c `PIN_MEASURE_BUTTON` |
| 串口 | USB-JTAG（板载 Type-C） | sdkconfig: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` |

### 1.2 AS7341 光谱传感器探头（外接，4 线）

| 探头引脚 | 接新板 | 说明 |
|---|---|---|
| VCC | 3.3V | 探头板供电（不可接 5V） |
| GND | GND | 共地 |
| SDA | **42**（I2C0 SDA） | 与触摸共用总线 |
| SCL | **41**（I2C0 SCL） | 与触摸共用总线 |

- I2C 地址 **0x39**，WHOAMI 寄存器返回 0x24
- **LED 光源不是 GPIO**：由 AS7341 寄存器 0x74（SW LED 控制）驱动，测量时 8mA 点亮 30ms，改接引脚无需处理 LED
- 探测失败（0x39 无应答）时固件自动进入 **SIM 模式**（模拟数据，记录 sensor 字段标 `SIM-DEMO`），不会崩溃
- 开机有 I2C 总线扫描诊断（main.c，打印 `I2C 扫描(41/42):` 后列出全部应答地址），用于快速定位接线问题

---

## 2. 迁移改引脚：改哪里

| 要改什么 | 文件 | 说明 |
|---|---|---|
| I2C 引脚（AS7341 + 触摸） | `components/esp_bsp/bsp_i2c.h` | `EXAMPLE_PIN_I2C_SDA/SCL`，两行即可 |
| LCD 引脚 | `components/esp_bsp/bsp_display.h` | 若新板屏幕控制器不是 JD9853，还需更换 `esp_lcd_jd9853` 组件并同步改 `bsp_display.c` 的面板 IO/初始化序列 |
| 触摸芯片 | `components/esp_bsp/bsp_touch.c` | 若新板触摸不是 AXS5106，更换 `esp_lcd_touch_axs5106` 组件；触摸方向不对改 main.c 里 `swap_xy/mirror_x/mirror_y` |
| TF 卡引脚 | `components/esp_bsp/bsp_sdcard.h` | SDMMC 4 线；若新板只有 1 线（SPI 模式），需改 `bsp_sdcard.c` 为 SDSPI 挂载 |
| 测量按键 | `main/main.c` `PIN_MEASURE_BUTTON` | 若新板无按键可删掉 `measure_button_init()`，仅保留触摸操作 |
| 串口 | `sdkconfig` | 非 USB-JTAG 板改 `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` |

**AS7341 驱动不需要改引脚**——它拿的是 `bsp_i2c_init()` 返回的总线句柄（main.c 中传给 `as7341_init()`），总线引脚变了它自动跟随。

> 注意：as7341.c 顶部有一行旧注释提到"独立 GPIO1/2"，那是历史遗留文案，实际始终使用共享 I2C0 总线，以 bsp_i2c.h 为准。

---

## 3. 软件环境

- **ESP-IDF v5.5**（v5.3.1+ 亦可，`button`/`esp_lcd` API 兼容）
  激活: `. <你的IDF路径>/export.sh`
- 组件（已在 `components/` 内，随工程走）: `esp_bsp`（板级抽象）、`esp_lcd_jd9853`、`esp_lcd_touch_axs5106`、`lvgl_ui`（外壳）；LVGL 8.4 来自 idf 组件管理（见 `dependencies.lock`）
- `main/CMakeLists.txt` REQUIRES: `esp_bsp lvgl esp_lvgl_port nvs_flash fatfs driver button`
- sdkconfig 关键项（`sdkconfig.defaults`）:
  - `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` —— 新板 Flash 容量不同必须同步改
  - `CONFIG_PARTITION_TABLE_CUSTOM=y` + 根目录 `partitions.csv`（nvs/phy/factory 6MB app）
  - `CONFIG_SPIRAM=y` + `CONFIG_SPIRAM_MODE_OCT=y`（非 Octal PSRAM 板要改）
  - `CONFIG_FATFS_LFN_HEAP=y` —— **必须**，否则长文件名 fopen 全部失败
  - `CONFIG_LV_COLOR_16_SWAP=y` —— SPI 屏字节序
  - `CONFIG_LV_MEM_CUSTOM=y`（LVGL 用系统堆，防列表页堆耗尽）
  - `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`
- 字体: LVGL 内置 montserrat 12/16/20/22/26 + 自制 Hiragino Sans GB 中文字库（见 §7）

---

## 4. 代码架构与数据流

### 4.1 模块清单（main/）

| 文件 | 职责 |
|---|---|
| `main.c` | 启动链（I2C→I2C扫描→AS7341→TF→色卡→时基→LCD→触摸→LVGL→GUI→按键），USB CDC JSONL 输出，BOOT 键 |
| `as7341.c` | AS7341 驱动：SMUX 双表、ATIME=29/ASTEP=599/GAIN=16X、LED 8mA/30ms、10 帧连读、失败软恢复重试、探测 3×250ms 重试、SIM 模式 |
| `model.c` + `model_weights.h` | V0.5 端侧模型 mlpB20/mt5（9→20ReLU→5，305 参数），输入 mean[10] 输出 R/G/B/冷暖/肤深 |
| `skin_match.c/h` + `skin_templates.h` | V0.3 查表最近邻（top-1 91.4%），保留用于 JSONL 对比，不参与 GUI |
| `recommend.c/h` | 粉底（欧莱雅 True Match 冷/暖 9 档双池）+ 口红 18 支多品牌库筛选 |
| `measurement.c` | 测量任务：QC→模型→推荐→TF 保存→GUI 回调（会话 ID 过滤）、命令队列、AUTO 连测 |
| `pantone.c` + `pantone_builtin.c` | 色卡库：优先 TF `/config/pantone_skintone.csv`，缺失回退内置 139 条 |
| `sd_manager.c` | TF 挂载、`/sdcard/data_v2/YYYYMMDD/records.txt` 追加写、时基扫描 |
| `gui.c/h` | LVGL 13 页界面 + splash，320×172，会话过滤 |
| `font_zh_*.c` ×7 | 14/14b/16/18/20/22/24px 中文字库 |

### 4.2 启动流程（app_main）

```
NVS → bsp_i2c_init(GPIO41/42) → I2C 总线扫描(诊断打印)
   → as7341_init(探测失败→SIM模式) → sdman_init(SDMMC 挂载)
   → pantone_load(TF CSV 优先/内置回退) → 时基(见 §5)
   → bsp_display_init(JD9853 320×172) → 触摸初始化 → LVGL 启动
   → gui_init + 回调注册 → 测量按键 GPIO7 → 主页
```

任一环节失败只打日志并在 GUI 显示，不 panic。

### 4.3 单次测量流程（measurement.c run_meas）

```
1. s_session++ (会话ID, GUI 丢弃迟到旧回调)
2. LED 亮 30ms (寄存器0x74, 失败重试3次)
3. 读 10 帧 10 通道 -> mean/std/CV
4. QC: 通道均值总和<200 → "信号不足"; cv_max>0.10 → "信号波动较大";
      有效帧=0 → "传感器读数失败"  [失败 → gui_meas_failure, 不出推荐]
5. model_predict(mean) → R/G/B + 冷暖 + dep16
6. recommend_v05() → 粉底 2-3 候选(主推±相邻档) + 口红 1 支
7. sdman_save() 追加 records.txt (TF 失败不影响结果页, 可在"更多"页重试)
8. gui_meas_result() / USB JSONL 输出
9. AUTO 模式自动下一张色卡; 单测模式停留结果页
```

---

## 5. 数据记录格式（TF 卡）

### 5.1 目录结构

```
/sdcard/
├── config/pantone_skintone.csv      # 可选, 缺失用内置139条
├── data/                            # V0.3 旧数据(685条), 不再读写, 保留
└── data_v2/                         # V0.5 新记录, 序号从 1 起
    └── YYYYMMDD/
        ├── records.txt              # 每天一个, 每次测量追加一行, 永不覆盖
        ├── session_index.txt        # 当天已测色卡索引
        └── <时间>_<色号>.txt        # 单次明细
```

### 5.2 records.txt 字段

`seq | 日期时间 | 色号 | mode | sensor | f1..f8 | clear | NIR | CV | R,G,B | 冷暖 | dep16 | 粉底 | 口红`

- `sensor`: `AS7341-V2`（真实）/ `SIM-DEMO`（模拟，探头未接时）
- `mode`: 单测 / AUTO / 工程模式采集

### 5.3 时序续接规则

板上无 RTC。开机扫描 `/sdcard/data_v2/**/records.txt`：
- 有记录 → 系统时间 = 最新记录时间 + 1s，新记录永远排在历史之后
- 无记录（空 TF）→ 从基线 2026-01-01 00:00:00 起
- 旧 `/sdcard/data/` 完全隔离，不参与扫描

---

## 6. 模型与推荐（V0.5）

- **模型**: mlpB20/mt5，输入 8 通道可见光光谱构造 `spec8(sqrt归一化) + ln(sum(f1..f8))` 9 维特征，隐层 20 ReLU，输出 R/G/B(0..1)/冷暖得分(>0.5冷调)/dep16(0..104)。验证: ΔE 均值 1.85、冷暖 98.9%、肤深 MAE 4.24。C 实现用 double 累加，与 Python/JS 逐位一致（权重由 tools/gen_weights_v05.py 从 color_model.json 生成 float32 精确表示）
- **粉底**: 冷/暖双池各 9 档（欧莱雅 True Match 1C-9C / 1W-9W），按模型冷暖选池 → RGB 最近色为主推 + 相邻深浅各一档
- **口红**: 18 支多品牌库（欧莱雅纷泽 8 / MAC 5 / Dior 2 / 阿玛尼 405 / TF 16 / YSL 1966），按底调+肤深区间过滤 → 取明度最接近"肤色明度-25"的单支；区间空或最优差>40 放宽全池

---

## 7. tools/ 生成脚本（重新生成方法）

| 脚本 | 功能 | 用法 |
|---|---|---|
| `gen_pantone_builtin.py` | CSV → 固件内置色卡库 `main/pantone_builtin.c` | `python3 tools/gen_pantone_builtin.py`（改色卡后重跑） |
| `gen_weights_v05.py` | color_model.json → `main/model_weights.h` | 需要 `皮肤数据/analysis_out/color_model.json`，路径在脚本内 |
| `gen_fonts_v05.py` | Hiragino TTC → 7 个中文字库 | 需要字体文件与 npx lv_font_conv，路径在脚本内 |
| `pantone_skintone.csv` | 139 条色卡源数据（TF 卡同路径拷贝） | — |
| `skinpen_receiver.py` | Mac 端 USB 串口接收 JSONL | `python3 tools/skinpen_receiver.py /dev/cu.usbmodem*` |

---

## 8. 迁移到新硬件 Checklist

1. 新板 Flash/PSRAM 容量核对 → 改 `sdkconfig.defaults`（FLASHSIZE / SPIRAM 模式）
2. 新板屏幕/触摸型号 → 换 `components/` 内对应 lcd/touch 组件，改 `esp_bsp` 引脚头
3. 按 §1.1 改所有 GPIO（多数情况下只改 bsp_*.h 即可）
4. AS7341 探头接新板 I2C 引脚（§1.2），或把 `bsp_i2c.h` 改成探头独享引脚并新建第二条总线（main.c 把句柄传给 `as7341_init` 与触摸初始化）
5. TF 卡接线/挂载方式核对（SDMMC 4 线 vs SDSPI）
6. `idf.py set-target esp32s3 && idf.py menuconfig`（确认 sdkconfig.defaults 已生效）→ `idf.py build`
7. 首次上电看串口自检: `I2C 扫描` 应含 0x39（AS7341）与触摸地址；`Pantone: 139 条`；`TF 就绪`
8. 测一次验证: LED 亮 → 结果页颜色正常（肤色圆块/粉底/口红为真实色彩，非绿蓝）→ 更多页光谱曲线 → records.txt 出现 seq=1

---

## 9. 构建与烧录

```bash
. <IDF路径>/export.sh
cd skinpen_v2
idf.py set-target esp32s3        # 首次或换芯片
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor   # 端口用 ls /dev/cu.* 确认
```

进入下载模式失败时: 按住 BOOT 键再按 RST（或重新插拔 USB）后重试。
**不要**对已存数据的卡执行 erase-flash / erase-otadata 以外的整盘擦除。

---

## 10. 已知限制

- 无 RTC，时间基于 TF 记录续接；空 TF 从 2026-01-01 基线起
- AS7341 与触摸共用总线，探头接线松动会连带影响触摸初始化时序（探测有 3×250ms 重试兜底）
- SIM 模式下所有流程可跑通，但记录 sensor 字段为 `SIM-DEMO`，用于演示/排查，不算有效数据
- 液晶控制器 JD9853 与触摸 AXS5106 为 1.47 寸板专属；换屏必须换组件
