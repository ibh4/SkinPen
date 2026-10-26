// 美妆推荐 V0.5 —— 巴黎欧莱雅为主 + 经典大牌 粉底液/口红色号推荐 (全部离线, 固件内置)
// 推荐依据(2026-10 网上查询):
//   粉底: True Match 选色体系(欧莱雅官网) —— 数字=深浅(1浅->11深, 本池 1-9),
//         字母=底调: C=冷粉调 / W=暖金调; 冷暖由 V0.5 模型输出判定, 禁止颜色反推
//   口红: 欧莱雅纷泽雾感唇膏在售色号 + MAC/Dior/阿玛尼/TF/YSL 经典色
// 口红选择算法: 底调过滤(R=冷粉/Y=暖金) + 肤深区间(dep16 与 [dmin*16-8, dmax*16+8] 比较),
//              候选按 |明度-(肤色明度-25)| 升序; 区间空或最优差>40 时放宽到全底调池;
//              输出候选列表供"换一支"轮换 —— 每支候选都满足真实筛选条件
#include "recommend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define FND_BRAND "欧莱雅 True Match"

// ---- 粉底液 True Match 色池 (数字 1浅->9深) ----
// 冷调 C: 粉/玫瑰底, 对应模型冷暖输出 cool=true
static const rec_item_t FND_C[9] = {
    { "1C", "", FND_BRAND, RGB565(0xF3, 0xDE, 0xD2) },
    { "2C", "", FND_BRAND, RGB565(0xED, 0xCF, 0xBF) },
    { "3C", "", FND_BRAND, RGB565(0xE4, 0xBD, 0xA6) },
    { "4C", "", FND_BRAND, RGB565(0xD9, 0xAB, 0x92) },
    { "5C", "", FND_BRAND, RGB565(0xCB, 0x96, 0x7A) },
    { "6C", "", FND_BRAND, RGB565(0xB9, 0x7F, 0x60) },
    { "7C", "", FND_BRAND, RGB565(0xA4, 0x69, 0x4C) },
    { "8C", "", FND_BRAND, RGB565(0x8B, 0x55, 0x3C) },
    { "9C", "", FND_BRAND, RGB565(0x6D, 0x40, 0x30) },
};
// 暖调 W: 金/黄底, 对应 cool=false
static const rec_item_t FND_W[9] = {
    { "1W", "", FND_BRAND, RGB565(0xF4, 0xDE, 0xBC) },
    { "2W", "", FND_BRAND, RGB565(0xEC, 0xD0, 0xA6) },
    { "3W", "", FND_BRAND, RGB565(0xE2, 0xBD, 0x8E) },
    { "4W", "", FND_BRAND, RGB565(0xD6, 0xA8, 0x77) },
    { "5W", "", FND_BRAND, RGB565(0xC6, 0x92, 0x61) },
    { "6W", "", FND_BRAND, RGB565(0xB4, 0x7C, 0x4E) },
    { "7W", "", FND_BRAND, RGB565(0x9D, 0x66, 0x3E) },
    { "8W", "", FND_BRAND, RGB565(0x82, 0x52, 0x31) },
    { "9W", "", FND_BRAND, RGB565(0x64, 0x3D, 0x26) },
};

// ---- 口红库 18 支 (欧莱雅为主 + 经典大牌) ----
// warm/cool: 适合底调; dmin/dmax: 适用肤深档位(1浅-6深)
static const struct {
    const char *code; const char *name; const char *brand;
    uint16_t rgb565;
    uint8_t warm, cool, dmin, dmax;
} LIP[18] = {
    { "202",   "摩卡奶茶", "欧莱雅纷泽", RGB565(0xA9, 0x7A, 0x68), 1, 1, 1, 2 },
    { "213",   "亮丽粉",   "欧莱雅纷泽", RGB565(0xE2, 0x6A, 0x80), 0, 1, 1, 2 },
    { "215",   "蓝调正红", "欧莱雅纷泽", RGB565(0xC0, 0x29, 0x38), 0, 1, 5, 6 },
    { "217",   "肉桂色",   "欧莱雅纷泽", RGB565(0xB5, 0x6D, 0x4C), 1, 0, 2, 2 },
    { "233",   "玫瑰豆沙", "欧莱雅纷泽", RGB565(0xB6, 0x6A, 0x74), 1, 1, 2, 3 },
    { "239",   "辣椒红",   "欧莱雅纷泽", RGB565(0xCF, 0x5A, 0x3A), 1, 0, 3, 3 },
    { "300",   "红棕色",   "欧莱雅纷泽", RGB565(0xB0, 0x4F, 0x3E), 1, 1, 5, 5 },
    { "302",   "秋日落叶", "欧莱雅纷泽", RGB565(0xA3, 0x4A, 0x2E), 1, 0, 6, 6 },
    { "Sheer", "浅珊瑚",   "MAC",        RGB565(0xEF, 0x9D, 0x8D), 1, 1, 1, 1 },
    { "Chili", "小辣椒",   "MAC",        RGB565(0xB9, 0x54, 0x2F), 1, 0, 4, 5 },
    { "Ruby",  "蓝调红",   "MAC",        RGB565(0xC2, 0x3B, 0x48), 0, 1, 4, 4 },
    { "Teddy", "裸棕",     "MAC",        RGB565(0xB0, 0x6F, 0x5F), 1, 1, 2, 3 },
    { "Marra", "红棕",     "MAC",        RGB565(0xA0, 0x4F, 0x33), 1, 0, 5, 6 },
    { "999",   "正红",     "Dior",       RGB565(0xC6, 0x28, 0x28), 1, 1, 3, 4 },
    { "772",   "干玫瑰",   "Dior",       RGB565(0xC4, 0x76, 0x80), 0, 1, 2, 2 },
    { "405",   "烂番茄",   "阿玛尼红管", RGB565(0xD1, 0x5A, 0x3A), 1, 0, 2, 3 },
    { "TF16",  "斯嘉丽红", "TF",         RGB565(0xB2, 0x22, 0x26), 1, 1, 4, 5 },
    { "1966",  "复古红棕", "YSL小金条",  RGB565(0x92, 0x34, 0x2C), 1, 0, 5, 6 },
};

// rgb565 -> rgb888 (供色距/明度计算)
static void rgb565_to888(uint16_t c5, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t r5 = (c5 >> 11) & 0x1F, g6 = (c5 >> 5) & 0x3F, b5 = c5 & 0x1F;
    *r = (r5 << 3) | (r5 >> 2);
    *g = (g6 << 2) | (g6 >> 4);
    *b = (b5 << 3) | (b5 >> 2);
}

// Rec.601 感知明度 0-255
static int lum565(uint16_t c5)
{
    uint8_t r, g, b;
    rgb565_to888(c5, &r, &g, &b);
    return (299 * r + 587 * g + 114 * b) / 1000;
}

static int dist2(uint16_t c5, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t cr, cg, cb;
    rgb565_to888(c5, &cr, &cg, &cb);
    int dr = (int)cr - r, dg = (int)cg - g, db = (int)cb - b;
    return dr * dr + dg * dg + db * db;
}

static void fill_item(rec_item_t *o, const char *code, const char *name,
                      const char *brand, uint16_t rgb565)
{
    snprintf(o->code, sizeof(o->code), "%s", code);
    snprintf(o->name, sizeof(o->name), "%s", name ? name : "");
    snprintf(o->brand, sizeof(o->brand), "%s", brand);
    o->rgb565 = rgb565;
}

void recommend_v05(uint16_t skin565, bool cool, int dep16,
                   rec_item_t fnd_out[3], int *best_out,
                   rec_item_t lip_out[], int lip_max, int *lip_n_out)
{
    // ---- 粉底: 按冷暖选池 -> 主推最近色 -> 相邻深浅两档共 3 个 ----
    const rec_item_t *pool = cool ? FND_C : FND_W;
    uint8_t r, g, b;
    rgb565_to888(skin565, &r, &g, &b);
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 9; i++) {
        int dd = dist2(pool[i].rgb565, r, g, b);
        if (dd < bd) { bd = dd; best = i; }
    }
    int lo = (best > 0) ? best - 1 : 0;          // 浅一档(试色用, 边界去重)
    int hi = (best < 8) ? best + 1 : 8;          // 深一档(边界去重)
    fnd_out[0] = pool[lo];
    fnd_out[1] = pool[best];
    fnd_out[2] = pool[hi];
    if (best_out) *best_out = best;

    // ---- 口红: 底调+肤深筛选 -> 按 |明度-(肤色明度-25)| 升序候选 ----
    int lskin = lum565(skin565);
    int target = lskin - 25;
    int cand[18], ncand = 0;
    int li = -1, ld = 1 << 30;
    for (int i = 0; i < 18; i++) {               // 轮1: 底调+肤深区间内
        if (!(cool ? LIP[i].cool : LIP[i].warm)) continue;
        if (dep16 < LIP[i].dmin * 16 - 8 || dep16 > LIP[i].dmax * 16 + 8) continue;
        cand[ncand++] = i;
        int d = abs(lum565(LIP[i].rgb565) - target);
        if (d < ld) { ld = d; li = i; }
    }
    if (ncand == 0 || ld > 40) {                 // 轮2: 区间空/无接近 -> 全底调池
        ncand = 0;
        for (int i = 0; i < 18; i++) {
            if (!(cool ? LIP[i].cool : LIP[i].warm)) continue;
            cand[ncand++] = i;
        }
        li = -1; ld = 1 << 30;
        for (int k = 0; k < ncand; k++) {
            int d = abs(lum565(LIP[cand[k]].rgb565) - target);
            if (d < ld) { ld = d; li = cand[k]; }
        }
    }
    // 按明度距离升序(插入排序, n<=18), 主推在前
    for (int a = 1; a < ncand; a++) {
        int v = cand[a], dv = abs(lum565(LIP[v].rgb565) - target);
        int c = a - 1;
        while (c >= 0 && abs(lum565(LIP[cand[c]].rgb565) - target) > dv) {
            cand[c + 1] = cand[c]; c--;
        }
        cand[c + 1] = v;
    }
    int n = 0;
    if (lip_out && lip_n_out) {
        for (int k = 0; k < ncand && n < lip_max; k++)
            fill_item(&lip_out[n++], LIP[cand[k]].code, LIP[cand[k]].name,
                      LIP[cand[k]].brand, LIP[cand[k]].rgb565);
        *lip_n_out = n;
    }
    (void)li;
}
