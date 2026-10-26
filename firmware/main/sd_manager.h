#pragma once
// TF 卡数据保存 —— SkinPen V0.5 (横版/大量记录)
// 目录结构: /sdcard/config/pantone_skintone.csv           (色卡库,见 pantone.c)
//           /sdcard/data_v2/YYYYMMDD/records.txt          (当天全部测量, 每条追加一行)
//           /sdcard/data_v2/YYYYMMDD/session_index.txt    (当天已测索引)
// V0.5 起数据根目录为 /sdcard/data_v2/, 与旧 /sdcard/data/(历史数据) 完全隔离,
// 序号从 1 重新计数; records.txt 追加模式写入, 重启/换卡不覆盖。

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// 单次测量统计(10 通道: f1..f8, clear, nir)
typedef struct {
    uint32_t seq;
    char     code[16];
    char     name[48];
    uint16_t mean[10];        // 10 通道均值(四舍五入)
    float    std[10];
    float    cv[10];          // CV = std/mean
    uint16_t repeats[10][10]; // 10 次重复 × 10 通道原始值
    int      repeat_count;
    uint16_t gain;            // 倍数(16)
    uint16_t atime;
    uint16_t astep;
    uint16_t led_ma;
    bool     led_on;
    char     timestamp_iso[32];  // "2026-08-31T17:30:05+08:00"
    char     date_dir[9];        // "YYYYMMDD"(由 timestamp 派生)
    char     hhmmss[7];          // "173005"
    char     session_id[20];     // "20260831_A"
    char     status[8];          // "PASS"/"FAIL"
    float    cv_max;
} sd_meas_t;

// 挂载 TF 卡(内部调用 bsp_sdcard_init),返回错误码
esp_err_t sdman_init(void);

// TF 是否就绪
bool sdman_ready(void);

// 保存一条测量记录到当天 records.txt(追加一行, 不覆盖)。
// out_path 可为 NULL;非 NULL 时返回完整路径。
esp_err_t sdman_save(sd_meas_t *m, char *out_path, size_t out_len);

// 追加当天 session 索引: "seq,code,STATUS,filename"
esp_err_t sdman_session_index_append(const char *date_dir, uint32_t seq,
                                     const char *code, const char *status,
                                     const char *filename);

// 读取当天 session 索引回调式遍历(开机恢复已测状态用)
// 返回行数;回调返回非 0 时停止
typedef int (*sdman_index_fn)(uint32_t seq, const char *code, const char *status,
                              const char *filename, void *user);
int sdman_session_index_read(const char *date_dir, sdman_index_fn fn, void *user);

// 扫描 /sdcard/data_v2/ 全部日期目录的 records.txt, 取最新一条记录的时间戳。
// 用于开机时序续接: 系统时间基线从最新记录 +1s 继续, 保证新记录
// 始终排在已有记录之后(跨重启按时间顺序追加)。仅扫描 v2 目录, 旧历史不参与。
// epoch_out 返回最新记录 epoch 秒; total_out 可为 NULL, 返回历史总行数。
// 返回 true 找到历史记录; TF 未就绪/无历史返回 false
bool sdman_latest_record_time(int64_t *epoch_out, int *total_out);
