// TF 卡数据保存实现 —— SkinPen V0.3 (横版/大量记录)
// TXT: /sdcard/data_v2/YYYYMMDD/records.txt (每天一个, 每条测量追加一行, 不覆盖)
//      V0.5 起启用: 与旧 /sdcard/data/(历史 685 条) 完全隔离, 序号从 1 重新计数
// idx: /sdcard/data_v2/YYYYMMDD/session_index.txt
#include "sd_manager.h"

#include "as7341.h"
#include "bsp_sdcard.h"
#include "esp_check.h"
#include "esp_log.h"
#include <errno.h>
#include <dirent.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "sdman";
static bool s_ready;

static int ensure_dirs(const char *date_dir, char *dir_out, size_t dir_len)
{
    mkdir("/sdcard/data_v2", 0775);
    snprintf(dir_out, dir_len, "/sdcard/data_v2/%s", date_dir);
    if (mkdir(dir_out, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "建目录失败 %s: %s", dir_out, strerror(errno));
        return -1;
    }
    return 0;
}

esp_err_t sdman_init(void)
{
    bsp_sdcard_init();                 // 失败只打日志,不 panic
    if (bsp_sdcard_get_size() == 0) {
        ESP_LOGE(TAG, "TF 卡未就绪(容量=0), 数据将无法保存");
        return ESP_FAIL;
    }
    mkdir("/sdcard/config", 0775);
    mkdir("/sdcard/data_v2", 0775);
    s_ready = true;
    ESP_LOGI(TAG, "TF 就绪, 容量 %llu MB", (unsigned long long)(bsp_sdcard_get_size() >> 20));
    return ESP_OK;
}

bool sdman_ready(void) { return s_ready; }

esp_err_t sdman_save(sd_meas_t *m, char *out_path, size_t out_len)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "TF 未挂载");
    ESP_RETURN_ON_FALSE(m != NULL, ESP_ERR_INVALID_ARG, TAG, "参数为空");

    char dir[32];
    if (ensure_dirs(m->date_dir, dir, sizeof(dir)) != 0) return ESP_FAIL;

    // 每天一个 TXT, 追加模式写入: 每条测量新增一行, 永不覆盖历史数据
    char path[96];
    snprintf(path, sizeof(path), "%s/records.txt", dir);

    // 首次创建时写表头
    bool need_header = (access(path, F_OK) != 0);
    FILE *f = fopen(path, "a");                    // 追加,不清除已有记录
    ESP_RETURN_ON_FALSE(f != NULL, ESP_FAIL, TAG, "打开失败 %s", path);

    if (need_header) {
        fprintf(f, "timestamp,session_id,sensor,seq,code,name_en,"
                   "f1,f2,f3,f4,f5,f6,f7,f8,clear,nir,cv_max,status\n");
    }

    // 一行 = 一次测量: 时间 + 色号 + 10 通道均值 + 质量
    fprintf(f, "%s,%s,%s,%u,%s,%s,",
            m->timestamp_iso, m->session_id,
            as7341_is_sim() ? "SIM-DEMO" : "AS7341-V2",
            (unsigned)m->seq, m->code, m->name);
    for (int ch = 0; ch < 10; ch++) fprintf(f, "%s%u", ch ? "," : "", m->mean[ch]);
    fprintf(f, ",%.4f,%s\n", (double)m->cv_max, m->status);
    fclose(f);

    if (out_path && out_len) snprintf(out_path, out_len, "%s", path);
    ESP_LOGI(TAG, "CSV 追加 %s (seq=%u)", path, (unsigned)m->seq);
    return ESP_OK;
}

esp_err_t sdman_session_index_append(const char *date_dir, uint32_t seq,
                                     const char *code, const char *status,
                                     const char *filename)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "TF 未挂载");
    char dir[32];
    if (ensure_dirs(date_dir, dir, sizeof(dir)) != 0) return ESP_FAIL;

    char path[64];
    snprintf(path, sizeof(path), "%s/session_index.txt", dir);
    FILE *f = fopen(path, "a");                 // 追加,不清除当天历史
    ESP_RETURN_ON_FALSE(f != NULL, ESP_FAIL, TAG, "打开 %s", path);
    fprintf(f, "%03u,%s,%s,%s\n", (unsigned)seq, code, status, filename);
    fclose(f);
    return ESP_OK;
}

int sdman_session_index_read(const char *date_dir, sdman_index_fn fn, void *user)
{
    if (!s_ready) return 0;
    char path[64];
    snprintf(path, sizeof(path), "/sdcard/data_v2/%s/session_index.txt", date_dir);
    FILE *f = fopen(path, "r");
    if (f == NULL) return 0;

    char line[160];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        uint32_t seq = 0;
        char code[16] = { 0 }, status[8] = { 0 }, fname[80] = { 0 };
        // 001,SKIN001,PASS,20260831_173005_001_SKIN001.txt
        if (sscanf(line, "%"SCNu32",%15[^,],%7[^,],%79s", &seq, code, status, fname) < 3) continue;
        n++;
        if (fn && fn(seq, code, status, fname, user) != 0) break;
    }
    fclose(f);
    return n;
}

// ---------------------------------------------------------------- 时序续接
// 解析 ISO 时间戳 "YYYY-MM-DDTHH:MM:SS+08:00" -> epoch 秒
// 记录由 localtime_r(strftime) 生成, 此处用 mktime 反解, 时区口径一致
static bool parse_iso_epoch(const char *iso, int64_t *out)
{
    int Y, M, D, h, m, s;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) != 6) return false;
    struct tm tmv = { 0 };
    tmv.tm_year = Y - 1900;
    tmv.tm_mon = M - 1;
    tmv.tm_mday = D;
    tmv.tm_hour = h;
    tmv.tm_min = m;
    tmv.tm_sec = s;
    tmv.tm_isdst = -1;
    time_t t = mktime(&tmv);
    if (t < 0) return false;
    *out = (int64_t)t;
    return true;
}

// 读取一个 records.txt 的最后一行数据行, 解析时间戳
static bool read_last_timestamp(const char *path, int64_t *out, int *rows)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return false;
    char line[256], last[256] = { 0 };
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == 't') continue;   // 空行/表头(timestamp 开头)
        n++;
        snprintf(last, sizeof(last), "%s", line);
    }
    fclose(f);
    if (rows) *rows = n;
    if (last[0] == '\0') return false;
    // timestamp 为第一列
    char iso[32] = { 0 };
    if (sscanf(last, "%31[^,]", iso) != 1) return false;
    return parse_iso_epoch(iso, out);
}

bool sdman_latest_record_time(int64_t *epoch_out, int *total_out)
{
    if (!s_ready) return false;

    DIR *dp = opendir("/sdcard/data_v2");
    if (dp == NULL) return false;

    int64_t best = -1;
    int total = 0;
    struct dirent *e;
    while ((e = readdir(dp)) != NULL) {
        // 日期目录名 YYYYMMDD(8 位数字), 只含 records.csv
        const char *d = e->d_name;
        bool date_dir = strlen(d) == 8;
        for (int i = 0; date_dir && i < 8; i++)
            if (d[i] < '0' || d[i] > '9') date_dir = false;
        if (!date_dir) continue;

        char path[64];
        snprintf(path, sizeof(path), "/sdcard/data_v2/%.8s/records.csv", d);
        int64_t ts = 0;
        int rows = 0;
        if (read_last_timestamp(path, &ts, &rows)) {
            total += rows;
            if (ts > best) best = ts;
        }
    }
    closedir(dp);

    if (total_out) *total_out = total;
    if (best < 0) return false;
    if (epoch_out) *epoch_out = best;
    return true;
}
