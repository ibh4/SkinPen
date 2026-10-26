#pragma once
// AS7341-V2 光谱传感器驱动 —— SkinPen V0.2
// 自 V0.1 (skinpen_as7341_lcd) 移植,时序参数经过真机验证:
//   ATIME=29, ASTEP=599, GAIN=16X, SMUX LOW/HIGH 双表, 单次测量 ~66ms
// V0.3 变化:传感器改接板载丝印 SCL/SDA(GPIO41/42),与触摸共用 bsp I2C0 总线。

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>

// 一次完整采集得到的 11 通道光谱帧
typedef struct {
    uint16_t f1, f2, f3, f4, f5, f6, f7, f8;   // 415/445/480/515/555/590/630/680 nm
    uint16_t clear;                            // 清色通道
    uint16_t nir;                              // 近红外 910nm
    uint32_t smux_ms;                          // SMUX 切换耗时(诊断)
    uint32_t measure_ms;                       // 测量耗时(诊断)
} as7341_frame_t;

// 初始化:在 bsp I2C0 总线(41/42)上探测 0x39,校验 WHOAMI,写入测量配置
// 总线由 main 传入(与触摸共用),驱动不再自行创建
esp_err_t as7341_init(i2c_master_bus_handle_t bus);

// LED 电流设置,仅允许 4/8/12mA (芯片规范)
esp_err_t as7341_set_led_current(uint16_t ma);

// LED 开关(寄存器 BIT7)
esp_err_t as7341_led_set(bool on);

// 读一帧完整光谱(SMUX LOW + HIGH,失败自动软恢复重试一次)
esp_err_t as7341_read_frame(as7341_frame_t *frame);

// 是否处于 SIM 模式(真机未接,read_frame 返回模拟数据)
bool as7341_is_sim(void);
