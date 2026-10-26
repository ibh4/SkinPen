// AS7341-V2 驱动实现 —— 自 V0.1 真机验证代码移植
// 关键时序与寄存器操作保持不变,仅更换 I2C 总线为独立 GPIO1/2。
// SIM 模式: 未接传感器时自动进入,生成模拟光谱数据,
//           用于先行跑通 CSV 读取 / TXT 保存 / JSONL 全链路(demo)。
#include "as7341.h"
#include "pantone.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// ---------------------------------------------------------------- 引脚/寄存器
// 传感器接板载丝印 SCL/SDA 引脚(GPIO41/42),与触摸共用 bsp I2C0 总线,
// 由 main 传入总线句柄,本驱动只挂 0x39 设备,不再自行建总线。
#define AS7341_ADDR      0x39
#define AS7341_I2C_HZ    100000

#define AS7341_CHIP_ID   0x09
#define AS7341_WHOAMI    0x92
#define AS7341_ENABLE    0x80
#define AS7341_ATIME     0x81
#define AS7341_STATUS2   0xA3
#define AS7341_STATUS6   0xA7
#define AS7341_CFG0      0xA9
#define AS7341_CFG1      0xAA
#define AS7341_CFG6      0xAF
#define AS7341_CONFIG    0x70
#define AS7341_LED       0x74
#define AS7341_CH0_DATA_L 0x95
#define AS7341_ASTEP_L   0xCA
#define AS7341_CONTROL   0xFA

// V0.1 验证过的测量参数
#define AS7341_ATIME_VALUE  29
#define AS7341_ASTEP_VALUE  599
#define AS7341_GAIN_16X     5
#define AS7341_MEASURE_TIMEOUT_MS 500
#define AS7341_SMUX_TIMEOUT_MS    100

static const char *TAG = "as7341";
static i2c_master_dev_handle_t s_dev;
static bool s_sim = false;        // 模拟模式: 真机未接时的 demo 数据源

// ---------------------------------------------------------------- 基础读写
static esp_err_t wr_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = { reg, value };
    return i2c_master_transmit(s_dev, data, sizeof(data), pdMS_TO_TICKS(500));
}

static esp_err_t rd_reg(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, pdMS_TO_TICKS(500));
}

static esp_err_t rd_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, pdMS_TO_TICKS(500));
}

static esp_err_t wr_regs(uint8_t start_reg, const uint8_t *data, size_t len)
{
    uint8_t packet[21] = { 0 };
    if (len > sizeof(packet) - 1) return ESP_ERR_INVALID_SIZE;
    packet[0] = start_reg;
    memcpy(&packet[1], data, len);
    return i2c_master_transmit(s_dev, packet, len + 1, pdMS_TO_TICKS(500));
}

static esp_err_t upd_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old = 0;
    ESP_RETURN_ON_ERROR(rd_reg(reg, &old), TAG, "read 0x%02x", reg);
    return wr_reg(reg, (old & ~mask) | (value & mask));
}

// ---------------------------------------------------------------- 功能配置
static esp_err_t power_enable(bool enable)
{
    return upd_bits(AS7341_ENABLE, BIT(0), enable ? BIT(0) : 0);
}

static esp_err_t spectral_enable(bool enable)
{
    return upd_bits(AS7341_ENABLE, BIT(1), enable ? BIT(1) : 0);
}

// 寄存器 bank 切换:CONFIG/LED 在 bank1(需 CFG0 BIT4=1)
static esp_err_t set_bank(bool low_bank)
{
    uint8_t cfg0 = 0;
    ESP_RETURN_ON_ERROR(rd_reg(AS7341_CFG0, &cfg0), TAG, "read CFG0 bank");
    cfg0 = low_bank ? (cfg0 | BIT(4)) : (cfg0 & ~BIT(4));
    return wr_reg(AS7341_CFG0, cfg0);
}

static esp_err_t configure_measurement(void)
{
    ESP_RETURN_ON_ERROR(wr_reg(AS7341_ATIME, AS7341_ATIME_VALUE), TAG, "ATIME");
    ESP_RETURN_ON_ERROR(wr_reg(AS7341_ASTEP_L, AS7341_ASTEP_VALUE & 0xFF), TAG, "ASTEP lo");
    ESP_RETURN_ON_ERROR(wr_reg(AS7341_ASTEP_L + 1, AS7341_ASTEP_VALUE >> 8), TAG, "ASTEP hi");
    ESP_RETURN_ON_ERROR(wr_reg(AS7341_CFG1, AS7341_GAIN_16X), TAG, "GAIN 16X");
    return ESP_OK;
}

static esp_err_t soft_recover(void)
{
    ESP_LOGW(TAG, "AS7341 软恢复");
    (void)upd_bits(AS7341_ENABLE, BIT(1) | BIT(4), 0);
    (void)power_enable(false);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(power_enable(true), TAG, "recover PON");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(configure_measurement(), TAG, "recover cfg");
    return ESP_OK;
}

// ---------------------------------------------------------------- SMUX
// LOW 表: F1-F4 + (重复的 CLEAR/NIR)  HIGH 表: F5-F8 + CLEAR + NIR
static const uint8_t smux_low[20] = {
    0x30, 0x01, 0x00, 0x00,
    0x00, 0x42, 0x00, 0x00,
    0x50, 0x00, 0x00, 0x00,
    0x20, 0x04, 0x00, 0x30,
    0x01, 0x50, 0x00, 0x06
};
static const uint8_t smux_high[20] = {
    0x00, 0x00, 0x00, 0x40,
    0x02, 0x00, 0x10, 0x03,
    0x50, 0x10, 0x03, 0x00,
    0x00, 0x00, 0x24, 0x00,
    0x00, 0x50, 0x00, 0x06
};

static esp_err_t smux_apply(const char *stage, const uint8_t table[20], uint32_t *elapsed_ms)
{
    int64_t start_us = esp_timer_get_time();
    uint8_t enable = 0, cfg6 = 0;

    ESP_RETURN_ON_ERROR(upd_bits(AS7341_ENABLE, BIT(1), 0), TAG, "clear SP_EN");
    ESP_RETURN_ON_ERROR(rd_reg(AS7341_CFG6, &cfg6), TAG, "rd CFG6");
    ESP_RETURN_ON_ERROR(wr_reg(AS7341_CFG6, (cfg6 & ~(BIT(4) | BIT(3))) | (2 << 3)), TAG, "SMUX cmd");
    ESP_RETURN_ON_ERROR(wr_regs(0x00, table, 20), TAG, "SMUX table");
    ESP_RETURN_ON_ERROR(rd_reg(AS7341_ENABLE, &enable), TAG, "rd ENABLE");
    ESP_RETURN_ON_ERROR(wr_reg(AS7341_ENABLE, enable | BIT(4)), TAG, "SMUXEN");

    for (uint32_t i = 0; i < AS7341_SMUX_TIMEOUT_MS; ++i) {
        if (rd_reg(AS7341_ENABLE, &enable) == ESP_OK && (enable & BIT(4)) == 0) {
            *elapsed_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGE(TAG, "SMUX %s TIMEOUT", stage);
    return ESP_ERR_TIMEOUT;
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

static esp_err_t measure_six(const char *stage, uint16_t adc[6])
{
    uint8_t raw[12] = { 0 }, status2 = 0;

    ESP_RETURN_ON_ERROR(spectral_enable(true), TAG, "start %s", stage);
    vTaskDelay(pdMS_TO_TICKS(2));
    for (uint32_t i = 0; i < AS7341_MEASURE_TIMEOUT_MS; ++i) {
        ESP_RETURN_ON_ERROR(rd_reg(AS7341_STATUS2, &status2), TAG, "STATUS2 %s", stage);
        if (status2 & BIT(6)) {
            ESP_RETURN_ON_ERROR(rd_regs(AS7341_CH0_DATA_L, raw, sizeof(raw)), TAG, "ADC %s", stage);
            for (int ch = 0; ch < 6; ++ch) adc[ch] = le16(&raw[ch * 2]);
            ESP_RETURN_ON_ERROR(spectral_enable(false), TAG, "stop %s", stage);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGE(TAG, "MEASURE %s TIMEOUT STATUS2=0x%02X", stage, status2);
    (void)spectral_enable(false);
    return ESP_ERR_TIMEOUT;
}

// ---------------------------------------------------------------- 对外接口
esp_err_t as7341_init(i2c_master_bus_handle_t bus)
{
    if (bus == NULL) goto sim_enter;

    {
        const i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AS7341_ADDR,
            .scl_speed_hz = AS7341_I2C_HZ,
        };
        uint8_t whoami = 0;
        // 探测加重试: 开机共享 I2C 总线偶发瞬态繁忙/传感器卡死状态,
        // 3 次 × 250ms 仍失败才进 SIM 模式(避免"假 SIM": 灯不亮、数据是模拟的)
        bool probed = false;
        for (int t = 0; t < 3 && !probed; t++) {
            if (i2c_master_bus_add_device(bus, &dev_cfg, &s_dev) == ESP_OK &&
                rd_reg(AS7341_WHOAMI, &whoami) == ESP_OK &&
                (whoami & 0xFC) == (AS7341_CHIP_ID << 2)) {
                probed = true;
                break;
            }
            if (s_dev) { i2c_master_bus_rm_device(s_dev); s_dev = NULL; }
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        if (!probed) goto sim_enter;   // 摘除设备,进入 SIM;总线是 bsp 的,不能删
        ESP_LOGI(TAG, "AS7341 WHOAMI=0x%02X", whoami);
    }

    ESP_RETURN_ON_ERROR(upd_bits(AS7341_ENABLE, BIT(1) | BIT(4), 0), TAG, "清 SP_EN/SMUXEN");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(power_enable(true), TAG, "上电");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(configure_measurement(), TAG, "测量配置");
    ESP_LOGI(TAG, "AS7341 就绪: 0x39@I2C0(GPIO41/42共用触摸总线), ATIME=29 ASTEP=599 GAIN=16X");
    return ESP_OK;

sim_enter:
    s_sim = true;
    ESP_LOGW(TAG, "未探测到 AS7341 (0x39@GPIO41/42) -> 进入 SIM 模式(模拟数据 demo)");
    return ESP_OK;
}

// 是否处于 SIM 模式(真机未接)
bool as7341_is_sim(void)
{
    return s_sim;
}

esp_err_t as7341_set_led_current(uint16_t ma)
{
    if (s_sim) return ESP_OK;                        // SIM: 无真实寄存器
    if (ma != 4 && ma != 8 && ma != 12) {
        ESP_LOGW(TAG, "LED 电流仅允许 4/8/12mA, 拒绝 %umA", ma);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(set_bank(true), TAG, "bank1");
    uint8_t config = 0, led = 0;
    esp_err_t err = rd_reg(AS7341_CONFIG, &config);
    if (err == ESP_OK) err = wr_reg(AS7341_CONFIG, config | BIT(3));   // 使能 LED 通路
    if (err == ESP_OK) err = rd_reg(AS7341_LED, &led);
    if (err == ESP_OK) {
        uint8_t drive = (uint8_t)((ma - 4) / 2);                       // 4mA->0, 8mA->2, 12mA->4
        err = wr_reg(AS7341_LED, (led & BIT(7)) | (drive & 0x7F));
    }
    esp_err_t rerr = set_bank(false);
    return err == ESP_OK ? rerr : err;
}

esp_err_t as7341_led_set(bool on)
{
    if (s_sim) return ESP_OK;                        // SIM: 无真实寄存器
    ESP_RETURN_ON_ERROR(set_bank(true), TAG, "bank1");
    uint8_t config = 0, led = 0;
    esp_err_t err = rd_reg(AS7341_CONFIG, &config);
    if (err == ESP_OK) err = wr_reg(AS7341_CONFIG, config | BIT(3));
    if (err == ESP_OK) err = rd_reg(AS7341_LED, &led);
    if (err == ESP_OK) err = wr_reg(AS7341_LED, on ? (led | BIT(7)) : (led & ~BIT(7)));
    esp_err_t rerr = set_bank(false);
    return err == ESP_OK ? rerr : err;
}

static esp_err_t read_once(as7341_frame_t *f)
{
    uint16_t lo[6] = { 0 }, hi[6] = { 0 };
    uint32_t ms = 0;

    ESP_RETURN_ON_ERROR(smux_apply("LOW", smux_low, &ms), TAG, "SMUX LOW");
    f->smux_ms = ms;
    ESP_RETURN_ON_ERROR(measure_six("LOW", lo), TAG, "测 LOW");
    ESP_RETURN_ON_ERROR(smux_apply("HIGH", smux_high, &ms), TAG, "SMUX HIGH");
    f->smux_ms += ms;
    ESP_RETURN_ON_ERROR(measure_six("HIGH", hi), TAG, "测 HIGH");

    f->f1 = lo[0]; f->f2 = lo[1]; f->f3 = lo[2]; f->f4 = lo[3];
    f->f5 = hi[0]; f->f6 = hi[1]; f->f7 = hi[2]; f->f8 = hi[3];
    f->clear = hi[4]; f->nir = hi[5];
    return ESP_OK;
}

esp_err_t as7341_read_frame(as7341_frame_t *frame)
{
    // ---- SIM 模式: 生成模拟光谱(demo) ----
    // 典型反射式谱形(F5/555nm 峰值, Clear 最高, NIR 最低),
    // 每张色卡按 seq 派生 -0.4%/卡 的整体反射率(模拟肤色变深),
    // 帧内 ±0.3% 随机噪声 -> 10 帧 CV 约 0.2%,QC 会给 PASS。
    if (s_sim) {
        const card_record_t *cur = pantone_current();
        uint16_t seq = cur ? (uint16_t)cur->seq : 1;
        const uint32_t base[10] = { 4200, 5100, 6000, 7300, 8100, 7000, 5100, 4000, 10300, 3150 };
        double scale = 1.0 - (seq > 1 ? seq - 1 : 0) * 0.004;
        uint16_t *ch = &frame->f1;                // f1..f8,clear,nir 内存连续
        for (int i = 0; i < 10; i++) {
            int32_t val = (int32_t)(base[i] * scale) + (int32_t)(esp_random() % 25) - 12;
            ch[i] = val < 0 ? 0 : (uint16_t)val;
        }
        frame->smux_ms = 2;
        frame->measure_ms = 66;
        return ESP_OK;
    }
    memset(frame, 0, sizeof(*frame));
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = read_once(frame);
    if (err != ESP_OK) {                       // 失败软恢复后重试一次(V0.1 策略)
        ESP_LOGW(TAG, "读取失败(%s), 软恢复后重试", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(soft_recover(), TAG, "软恢复");
        memset(frame, 0, sizeof(*frame));
        err = read_once(frame);
    }
    if (err == ESP_OK) {
        frame->measure_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        ESP_LOGI(TAG, "帧完成 smux=%ums measure=%ums F=%u,%u,%u,%u,%u,%u,%u,%u CLR=%u NIR=%u",
                 (unsigned)frame->smux_ms, (unsigned)frame->measure_ms,
                 frame->f1, frame->f2, frame->f3, frame->f4, frame->f5,
                 frame->f6, frame->f7, frame->f8, frame->clear, frame->nir);
    }
    return err;
}
