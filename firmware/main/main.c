// SkinPen V0.5 主入口 —— 端侧模型光谱测肤色 + 粉底/口红推荐
// 硬件: ESP32-S3-Touch-LCD-1.47 (JD9853 172x320 + AXS5106 触摸)
//       AS7341-V2 (I2C0 共享: GPIO41/42, 0x39) + TF 卡 (SDMMC)
// 数据流: 测量 -> TF 卡 CSV(不覆盖) + USB CDC JSONL(Mac 实时接收)
// V0.5: 13 页新界面(交接包《界面定稿与移植规范》) + mlpB20/mt5 模型直出
//       (model.c) + recommend_v05 离线推荐
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "as7341.h"
#include "gui.h"
#include "measurement.h"
#include "pantone.h"
#include "sd_manager.h"

#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_sdcard.h"
#include "bsp_touch.h"
#include "nvs_flash.h"

#include "button_gpio.h"
#include "iot_button.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

#define LCD_H_RES 172
#define LCD_V_RES 320
#define DRAW_BUFF_LINES 50

// 上电时间基准: 板上无 RTC,以固定日期为基线,时间戳随运行时长推进。
// (Mac 端接收时如需精确时间,可按接收时刻覆盖)
#define BOOT_EPOCH_20260101 1767225600L

static const char *TAG = "skinpen2";

// GUI -> 业务回调(只投递命令,不阻塞 LVGL)
static void on_start(void)      { measurement_cmd_start(); }       // prepare 页开始采集
static void on_cancel(void)     { measurement_abort(); }           // 采集页取消
static void on_capture(void)    { measurement_cmd_start(); }       // 工程页采集此卡
static void on_prev(void)       { measurement_cmd_prev(); }
static void on_next(void)       { measurement_cmd_next(); }
static void on_select(int idx)  { measurement_cmd_select(idx); }
static void on_save_retry(void) { measurement_cmd_save_retry(); }  // 更多页保存重试

// 物理测量键 GPIO7: 短按=开始采集, 长按=取消
#define PIN_MEASURE_BUTTON 7

static void btn_short_cb(void *arg, void *data)
{
    (void)arg; (void)data;
    measurement_cmd_start();
}

static void btn_long_cb(void *arg, void *data)
{
    (void)arg; (void)data;
    measurement_abort();
}

static void measure_button_init(void)
{
    const button_config_t btn_cfg = { 0 };
    const button_gpio_config_t gpio_cfg = {
        .gpio_num = PIN_MEASURE_BUTTON,
        .active_level = 0,               // 按键按下接地(内部上拉)
    };
    button_handle_t btn = NULL;
    if (iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &btn) != ESP_OK || btn == NULL) {
        ESP_LOGE(TAG, "测量键 GPIO7 初始化失败");
        return;
    }
    iot_button_register_cb(btn, BUTTON_PRESS_DOWN, NULL, btn_short_cb, NULL);
    iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, NULL, btn_long_cb, NULL);
    ESP_LOGI(TAG, "测量键就绪: GPIO7 短按=MEASURE 长按=CANCEL");
}

void app_main(void)
{
    // NVS(Wi-Fi/BLE 库初始化需要,虽本设备离线)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 时间基线
    struct timeval tv = { .tv_sec = BOOT_EPOCH_20260101, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    // ---- 显示 + 触摸 + LVGL ----
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    i2c_master_bus_handle_t i2c_bus = bsp_i2c_init();          // 触摸总线 GPIO41/42

    // I2C 总线扫描诊断: 打印当前应答的全部设备地址(判断探头是否电气连通)
    {
        int found = 0;
        printf("I2C 扫描(41/42):");
        for (int a = 0x03; a <= 0x77; a++) {
            if (i2c_master_probe(i2c_bus, a, 20) == ESP_OK) {
                printf(" 0x%02X", a);
                found++;
            }
        }
        printf(found ? "\n" : "  (无设备)\n");
    }
    bsp_display_init(&io_handle, &panel_handle, LCD_H_RES * DRAW_BUFF_LINES);

    // 整屏清除 GRAM 残留(上电随机数据表现为竖条纹/花屏)
    // 填白色而非黑色: 与浅色主题背景一致, 避免"黑屏一闪"的观感
    {
        size_t chunk = LCD_H_RES * DRAW_BUFF_LINES;
        uint16_t *blk = heap_caps_malloc(chunk * sizeof(uint16_t), MALLOC_CAP_DMA);
        if (blk) {
            for (int i = 0; i < (int)chunk; i++) blk[i] = 0xFFFF;   // RGB565 白
            for (int y = 0; y < LCD_V_RES; y += DRAW_BUFF_LINES) {
                int h = (LCD_V_RES - y < DRAW_BUFF_LINES) ? LCD_V_RES - y : DRAW_BUFF_LINES;
                esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_H_RES - 1, y + h - 1, blk);
            }
            heap_caps_free(blk);
        }
    }

    // 90° 横屏: 172x320 可见区在 GRAM (34,0) 起;控制器 swap+mirror 后 x/y 对调,
    // 34px 偏移移到 y 轴 -> gap(0,34)(官方 Demo 90° 配置, 与 disp_cfg.rotation 配套)
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 34));

    bsp_touch_init(&touch_handle, i2c_bus, LCD_H_RES, LCD_V_RES, 90); // 触摸随横屏 90° 旋转
    bsp_display_brightness_init();
    bsp_display_set_brightness(48);   // 48 = 原 80% 的 60%, 按需求调低

    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 1024 * 10,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // 横屏方案(官方 Demo 同款): LCD 控制器硬件旋转(swap_xy+mirror), 不用 sw_rotate。
    // esp_lvgl_port add_disp 时把 rotation 应用到 panel(MADCTL), LVGL 逻辑屏直接为
    // 320x172 横版, flush 坐标由 panel 硬件变换, 触摸输出无需补偿即可对齐逻辑屏。
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_V_RES * DRAW_BUFF_LINES,   // 逻辑屏行宽 320
        .double_buffer = true,
        .hres = LCD_V_RES,                            // 横屏逻辑宽 320
        .vres = LCD_H_RES,                            // 横屏逻辑高 172
        .monochrome = false,
        .rotation = { .swap_xy = true, .mirror_x = true, .mirror_y = false },  // 90° 官方组合
        .flags = { .buff_spiram = false, .buff_dma = true },
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_ERROR_CHECK(disp == NULL ? ESP_FAIL : ESP_OK);

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = touch_handle,
    };
    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg);
    ESP_ERROR_CHECK(indev == NULL ? ESP_FAIL : ESP_OK);

    // ---- 启动自检输出(串口, 供调试与 Mac 端核对) ----
    printf("LCD: %dx%d (landscape 320x172, hw rotate 90 gap(0,34))\n", LCD_H_RES, LCD_V_RES);
    printf("LVGL: OK (buf=%d lines x2, DMA)\n", DRAW_BUFF_LINES);

    // ---- 外设: TF 卡 -> 色卡库 -> AS7341 ----
    // TF 卡座接触偶发不良: 挂载失败自动重试 3 次(间隔 300ms)
    bool sd_ok = false;
    for (int i = 0; i < 3 && !sd_ok; i++) {
        sd_ok = (sdman_init() == ESP_OK);
        if (!sd_ok) { ESP_LOGW(TAG, "TF 挂载失败,重试 %d/3", i + 1); vTaskDelay(pdMS_TO_TICKS(300)); }
    }
    printf("TF: %s\n", sd_ok ? "OK" : "FAIL");

    int card_count = 0;
    esp_err_t perr = ESP_FAIL;
    for (int i = 0; i < 3 && perr != ESP_OK; i++) {
        perr = pantone_load(&card_count);
        if (perr != ESP_OK) { ESP_LOGW(TAG, "CSV 读取失败,重试 %d/3", i + 1); vTaskDelay(pdMS_TO_TICKS(300)); }
    }
    bool pt_ok = (perr == ESP_OK);
    if (!pt_ok) ESP_LOGE(TAG, "色卡库加载失败: %s (检查 TF /config/pantone_skintone.csv)",
                         esp_err_to_name(perr));
    printf("Pantone: %d loaded\n", pt_ok ? card_count : 0);

    // 共享 I2C0 (41/42): 开机时触摸控制器等还在占用总线, 首次探测可能瞬态失败,
    // 重试 3 次(间隔 250ms), 避免"假 SIM 模式"(灯不亮、数据是模拟的)
    bool as_ok = false;
    for (int i = 0; i < 3 && !as_ok; i++) {
        as_ok = (as7341_init(i2c_bus) == ESP_OK);
        if (!as_ok) { ESP_LOGW(TAG, "AS7341 探测失败,重试 %d/3", i + 1); vTaskDelay(pdMS_TO_TICKS(250)); }
    }
    if (!as_ok) ESP_LOGE(TAG, "AS7341 初始化失败 (检查 GPIO41/42 SCL/SDA 接线)");
    else as7341_set_led_current(8);                 // 默认 LED 电流 8mA
    printf("AS7341: %s\n", as_ok ? (as7341_is_sim() ? "SIM MODE (demo)" : "OK") : "FAIL");

    // ---- 时序续接: 从 TF 历史最新记录 +1s 继续系统时间 ----
    // 使新记录始终排在历史记录之后(跨重启按时间顺序追加), 不再回到开机基线
    if (sd_ok) {
        int64_t last_epoch = 0;
        int total = 0;
        if (sdman_latest_record_time(&last_epoch, &total)) {
            time_t now = time(NULL);
            if (last_epoch + 1 > (int64_t)now) {
                struct timeval tv2 = { .tv_sec = (time_t)last_epoch + 1, .tv_usec = 0 };
                settimeofday(&tv2, NULL);
            }
            time_t cur = time(NULL);
            struct tm tmc;
            localtime_r(&cur, &tmc);
            char tsbuf[24];
            strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", &tmc);
            printf("TimeBase: %d 条历史记录, 当前时间续接至 %s\n", total, tsbuf);
        } else {
            printf("TimeBase: 无历史记录, 使用默认基线 2026-01-01\n");
        }
    }

    // ---- GUI + 采集任务 ----
    ESP_ERROR_CHECK(gui_init());
    gui_set_dev_status(sd_ok, card_count, as_ok);   // 异常时主页显示红字
    if (pt_ok) {
        gui_fill_cards(card_count);
        gui_set_card(pantone_index(), pantone_count(), pantone_current());
    }

    gui_register_cbs(&(gui_cbs_t){
        .on_start = on_start, .on_cancel = on_cancel, .on_capture = on_capture,
        .on_prev = on_prev, .on_next = on_next, .on_select = on_select,
        .on_save_retry = on_save_retry,
    });

    // 采集任务(内部恢复当天 session_index 已测状态)
    ESP_ERROR_CHECK(measurement_init());
    measure_button_init();

    // JSONL hello: Mac 端据此确认设备上线(sim=true 表示模拟数据 demo)
    printf("{\"type\":\"hello\",\"device\":\"SkinPen V0.5\",\"firmware\":\"0.5.0\","
           "\"cards\":%d,\"sd\":%s,\"as7341\":%s,\"sim\":%s}\n",
           card_count, sd_ok ? "true" : "false", as_ok ? "true" : "false",
           (as_ok && as7341_is_sim()) ? "true" : "false");
    ESP_LOGI(TAG, "SkinPen V0.5 就绪");
}
