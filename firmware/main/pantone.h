#pragma once
// PANTONE SkinTone 色卡库 —— 从 TF 卡 CSV 加载,禁止硬编码色卡
// CSV: /sdcard/config/pantone_skintone.csv
// 字段: seq,code,name_en,enabled

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    int  seq;                 // 1..138
    char code[16];            // "1R00SP"
    char name_en[48];         // "PANTONE 1R00 SP"
    bool enabled;
} card_record_t;

// 从 TF 加载色卡库。成功返回 ESP_OK,同时给出条目数
esp_err_t pantone_load(int *out_count);

// 当前库内色卡数(enabled 的条目数)
int pantone_count(void);

// 当前选中索引(0 起)
int pantone_index(void);

// 导航: 下一张/上一张(循环),返回新索引
int pantone_next(void);
int pantone_prev(void);

// 跳到指定索引(选择页点选),自动夹取范围
void pantone_select(int index);

// 取当前选中色卡(常量指针,内部存储)
const card_record_t *pantone_current(void);

// 按索引取色卡
const card_record_t *pantone_get(int index);
