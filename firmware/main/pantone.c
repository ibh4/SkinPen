// 色卡库实现 —— SkinPen V0.5
// 数据源优先级: TF 卡 /sdcard/config/pantone_skintone.csv (可自行增删)
//               -> 缺失/无效时回退固件内置 pantone_builtin.c (139 条, 由 tools 脚本生成)
// 加载规则: 跳过表头与 UTF-8 BOM -> 只收 enabled 项 -> 按 seq 升序排序 -> 重复 seq/code 检查
#include "pantone.h"

#include "esp_check.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CSV_PATH   "/sdcard/config/pantone_skintone.csv"
#define MAX_CARDS  200

// 内置色卡库(tools/gen_pantone_builtin.py 从 CSV 生成)
extern const card_record_t PANTONE_BUILTIN[];
extern const int PANTONE_BUILTIN_COUNT;

static const char *TAG = "pantone";
static card_record_t s_cards[MAX_CARDS];   // 仅 enabled,按 seq 升序
static int s_count;
static int s_index;

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ')) s[--n] = 0;
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static int cmp_seq(const void *a, const void *b)
{
    const card_record_t *x = a, *y = b;
    return x->seq - y->seq;
}

// 回退: 复制固件内置色卡库到工作数组
static void load_builtin(void)
{
    s_count = 0;
    for (int i = 0; i < PANTONE_BUILTIN_COUNT && s_count < MAX_CARDS; i++) {
        s_cards[s_count++] = PANTONE_BUILTIN[i];
    }
    s_index = 0;
    ESP_LOGW(TAG, "使用固件内置色卡库: %d 条 (TF 配置缺失或无效)", s_count);
}

esp_err_t pantone_load(int *out_count)
{
    FILE *f = fopen(CSV_PATH, "r");
    if (f == NULL) {                            // TF 无配置 -> 内置库
        load_builtin();
        if (out_count) *out_count = s_count;
        return ESP_OK;
    }

    char line[128];
    s_count = 0;
    while (fgets(line, sizeof(line), f)) {
        // 跳过 UTF-8 BOM (EF BB BF, Excel/记事本导出的 CSV 常见)
        char *p = line;
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
        if (p[0] == '#' || p[0] == '\n' || p[0] == '\r' || p[0] == 0) continue;
        int seq = 0, enabled = 0;
        char code[16] = { 0 }, name[48] = { 0 };
        // seq,code,name_en,enabled  (name_en 允许缺失; 从 p 解析以跳过 BOM)
        int nf = sscanf(p, "%d,%15[^,],%47[^,],%d", &seq, code, name, &enabled);
        if (nf < 2 || seq <= 0) continue;         // 至少 seq+code
        trim(code); trim(name);
        if (code[0] == 0) continue;

        if (!(nf >= 4 && enabled == 0)) {         // 无 enabled 字段默认启用
            card_record_t *c = &s_cards[s_count];
            c->seq = seq;
            snprintf(c->code, sizeof(c->code), "%s", code);
            snprintf(c->name_en, sizeof(c->name_en), "%s", name);
            c->enabled = true;
            s_count++;
        }
        if (s_count >= MAX_CARDS) break;
    }
    fclose(f);

    if (s_count == 0) {                          // 文件存在但无有效行 -> 内置库
        ESP_LOGW(TAG, "CSV 无有效条目");
        load_builtin();
        if (out_count) *out_count = s_count;
        return ESP_OK;
    }

    // 按 seq 升序排序,保证 GUI 顺序 = 实体色卡顺序
    qsort(s_cards, s_count, sizeof(card_record_t), cmp_seq);

    // 重复 seq 检查(排序后相邻比较)
    for (int i = 1; i < s_count; i++) {
        if (s_cards[i].seq == s_cards[i - 1].seq) {
            ESP_LOGE(TAG, "重复 seq=%d", s_cards[i].seq);
            return ESP_ERR_INVALID_STATE;
        }
    }
    // 重复 code 检查
    for (int i = 0; i < s_count; i++) {
        for (int j = i + 1; j < s_count; j++) {
            if (strcmp(s_cards[i].code, s_cards[j].code) == 0) {
                ESP_LOGE(TAG, "重复 code=%s", s_cards[i].code);
                return ESP_ERR_INVALID_STATE;
            }
        }
    }

    s_index = 0;
    ESP_LOGI(TAG, "色卡库加载: %d 条 (来自 %s, seq %d~%d)",
             s_count, CSV_PATH, s_cards[0].seq, s_cards[s_count - 1].seq);
    if (out_count) *out_count = s_count;
    return ESP_OK;
}

int pantone_count(void) { return s_count; }
int pantone_index(void) { return s_index; }

int pantone_next(void)
{
    if (s_count == 0) return 0;
    s_index = (s_index + 1) % s_count;
    return s_index;
}

int pantone_prev(void)
{
    if (s_count == 0) return 0;
    s_index = (s_index - 1 + s_count) % s_count;
    return s_index;
}

void pantone_select(int index)
{
    if (s_count == 0) return;
    if (index < 0) index = 0;
    if (index >= s_count) index = s_count - 1;
    s_index = index;
}

const card_record_t *pantone_current(void)
{
    return s_count ? &s_cards[s_index] : NULL;
}

const card_record_t *pantone_get(int index)
{
    if (index < 0 || index >= s_count) return NULL;
    return &s_cards[index];
}
