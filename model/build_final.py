#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SkinPen 最终资产生成 v3
========================
1. records.csv 清洗 (cv_max<0.05 + 物理边界 + 卡内离群)
2. 模板 = sqrt(f1..f8) 归一化 (交叉验证 top-1 91.4% / top-3 97.1%)
3. PANTONE SkinTone 官方 RGB (photoshoplus, AdobeRGB1998) -> sRGB -> RGB565
4. 输出 ESP32 固件头文件 skin_templates.h (138 有数据卡)
"""
import csv, os, math
from collections import defaultdict
from statistics import median

BASE = "/Users/pwngwc/projects/ESP32/皮肤数据/皮肤光谱数据_extracted/皮肤光谱数据"
CSV_PATH = os.path.join(BASE, "DATA", "20260101", "records.csv")
LIB = "/Users/pwngwc/projects/ESP32/光谱/skinpen_v2/tools/pantone_skintone.csv"
OUT_H = "/Users/pwngwc/projects/ESP32/光谱/skinpen_v2/main/skin_templates.h"
OUT_TXT = "/Users/pwngwc/projects/ESP32/皮肤数据/analysis_out/final_report.txt"

CV_THR, SUM_MIN, CLEAR_MIN, CARD_THR = 0.05, 200, 80, 0.10

# ---------------- PANTONE SkinTone 138 官方 RGB (Adobe RGB 1998, photoshoplus.fr) ----------------
P = {
"1R00SP":(196,178,169),"1R01SP":(194,172,162),"1R02SP":(199,168,150),"1R03SP":(199,166,148),
"1R04SP":(195,163,143),"1R05SP":(191,159,140),"1R06SP":(187,153,131),"1R07SP":(183,146,123),
"1R08SP":(179,136,110),"1R09SP":(171,129,103),"1R10SP":(162,120,93),"1R11SP":(151,108,82),
"1R12SP":(134,96,72),"1R13SP":(119,86,66),"1R14SP":(100,75,60),"1R14.5SP":(95,74,58),"1R15SP":(84,67,56),
"1Y00SP":(198,174,161),"1Y01SP":(200,172,153),"1Y02SP":(200,169,149),"1Y03SP":(197,166,145),
"1Y04SP":(194,163,142),"1Y05SP":(190,157,134),"1Y06SP":(187,152,129),"1Y07SP":(182,146,121),
"1Y08SP":(177,136,108),"1Y09SP":(168,127,100),"1Y10SP":(160,118,90),"1Y11SP":(148,108,81),
"1Y12SP":(135,98,73),"1Y13SP":(119,87,65),"1Y14SP":(101,77,62),"1Y14.5SP":(100,73,60),"1Y15SP":(102,77,60),
"2R02SP":(203,166,149),"2R03SP":(202,165,150),"2R04SP":(197,161,146),"2R05SP":(194,156,140),
"2R06SP":(189,149,132),"2R07SP":(182,141,123),"2R08SP":(179,134,114),"2R09SP":(172,126,105),
"2R10SP":(165,117,95),"2R11SP":(154,107,84),"2R12SP":(140,98,78),"2R13SP":(120,84,68),
"2R14SP":(99,75,63),"2R14.5SP":(92,72,60),"2R15SP":(81,66,57),
"2Y00SP":(205,174,155),"2Y01SP":(203,171,153),"2Y02SP":(201,169,150),"2Y03SP":(200,167,147),
"2Y04SP":(195,162,143),"2Y05SP":(190,156,135),"2Y06SP":(189,153,131),"2Y07SP":(181,146,123),
"2Y08SP":(175,136,112),"2Y09SP":(169,129,103),"2Y10SP":(160,121,95),"2Y11SP":(152,111,84),
"2Y12SP":(137,99,76),"2Y13SP":(121,89,69),"2Y14SP":(109,80,57),"2Y14.5SP":(103,76,60),
"3R02SP":(206,165,149),"3R03SP":(203,163,149),"3R04SP":(198,161,149),"3R05SP":(195,154,141),
"3R06SP":(194,151,135),"3R07SP":(185,141,126),"3R08SP":(181,133,118),"3R09SP":(174,127,110),
"3R10SP":(165,116,97),"3R11SP":(153,106,88),"3R12SP":(137,95,77),"3R13SP":(119,84,69),
"3R14SP":(102,76,65),"3R14.5SP":(94,73,60),"3R15SP":(83,67,58),
"3Y01SP":(202,171,150),"3Y02SP":(200,169,148),"3Y03SP":(199,167,146),"3Y04SP":(194,163,143),
"3Y05SP":(190,157,136),"3Y06SP":(186,153,130),"3Y07SP":(180,146,119),"3Y08SP":(176,137,110),
"3Y09SP":(166,130,102),"3Y10SP":(155,119,93),"3Y11SP":(144,111,84),"3Y12SP":(132,99,73),
"3Y13SP":(118,87,63),"3Y14SP":(108,82,58),"3Y14.5SP":(107,80,58),
"4R04SP":(198,160,150),"4R05SP":(195,154,142),"4R06SP":(193,148,135),"4R07SP":(188,141,128),
"4R08SP":(181,134,120),"4R09SP":(173,126,113),"4R10SP":(168,119,100),"4R11SP":(152,107,91),
"4R12SP":(137,96,79),"4R13SP":(118,83,70),"4R14SP":(99,72,62),"4R14.5SP":(93,71,61),"4R15SP":(84,66,57),
"4Y01SP":(203,172,149),"4Y02SP":(199,169,146),"4Y03SP":(198,168,145),"4Y04SP":(194,164,141),
"4Y05SP":(188,157,135),"4Y06SP":(185,153,128),"4Y07SP":(179,146,118),"4Y08SP":(173,139,111),
"4Y09SP":(165,129,99),"4Y10SP":(153,121,93),"4Y11SP":(143,112,84),"4Y12SP":(131,101,72),
"5R04SP":(198,160,152),"5R05SP":(195,153,144),"5R06SP":(193,149,140),"5R07SP":(190,145,134),
"5Y01SP":(201,173,149),"5Y02SP":(197,169,145),"5Y03SP":(196,168,144),"5Y04SP":(192,164,141),
"5Y05SP":(189,159,135),"5Y06SP":(184,154,126),"5Y07SP":(177,145,117),"5Y08SP":(170,139,109),
"5Y09SP":(163,130,100),"5Y10SP":(155,125,93),
"6Y01SP":(203,176,148),"6Y02SP":(200,173,145),"6Y03SP":(194,168,142),"6Y04SP":(187,163,140),
}
assert len(P) == 138, len(P)

def adobe_to_srgb(ad):
    """Adobe RGB (1998) -> sRGB (修正矩阵: 中性灰不变性检验行和=1通过)"""
    M = ((1.39840, -0.39854,  0.00001),
         (0.00012,  1.00006, -0.00004),
         (0.00004, -0.04294,  1.04316))
    lin = [ (c/255.0) ** 2.19921875 for c in ad ]
    s = [ sum(M[i][j]*lin[j] for j in range(3)) for i in range(3) ]
    out = []
    for v in s:
        v = max(0.0, min(1.0, v))
        out.append(int(round((1.055*v**(1/2.4)-0.055)*255)) if v > 0.0031308 else int(round(12.92*v*255)))
    return out

def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

# ---------------- 库对齐 ----------------
lib = list(csv.DictReader(open(LIB, encoding="utf-8-sig")))
lib_codes = [r["code"] for r in lib]
missing_rgb = [c for c in lib_codes if c not in P]
extra_rgb = [c for c in P if c not in lib_codes]
print(f"库 {len(lib_codes)} 张 | 官方RGB {len(P)} 张 | 库中无RGB: {missing_rgb} | RGB表多余: {extra_rgb}")

# ---------------- 光谱清洗 + 模板 ----------------
CH = ["f1","f2","f3","f4","f5","f6","f7","f8","clear","nir"]
def rdist(a, b):
    nb = math.sqrt(sum(x*x for x in b))
    return math.sqrt(sum((a[i]-b[i])**2 for i in range(len(a))))/nb if nb > 1e-9 else float('inf')

rows = list(csv.DictReader(open(CSV_PATH, encoding="utf-8")))
stage = []
for r in rows:
    v = [float(r[c]) for c in CH]
    if float(r["cv_max"]) < CV_THR and sum(v) >= SUM_MIN and v[8] >= CLEAR_MIN:
        stage.append((int(r["seq"]), r["code"], v))
by_card = defaultdict(list)
for rec in stage: by_card[rec[1]].append(rec)
final = []
for code, recs in by_card.items():
    if len(recs) <= 2:
        final.extend(recs); continue
    med = [median([r[2][ch] for r in recs]) for ch in range(10)]
    good = [r for r in recs if rdist(r[2], med) <= CARD_THR] or [min(recs, key=lambda r: rdist(r[2], med))]
    final.extend(good)
byc = defaultdict(list)
for r in final: byc[r[1]].append(r)

def spec8(v):
    """sqrt(f1..f8) 归一化 -> 8 维模板向量"""
    s = [math.sqrt(max(0.0, x)) for x in v[:8]]
    t = sum(s)
    return [x/t for x in s] if t > 0 else None

templates = []   # (seq, code, spec8, rgb565, n)
for code in sorted(byc, key=lambda c: byc[c][0][0]):
    recs = byc[code]
    mean = [sum(r[2][ch] for r in recs)/len(recs) for ch in range(10)]
    sp = spec8(mean)
    rgb = adobe_to_srgb(P[code])
    templates.append((recs[0][0], code, sp, rgb565(rgb), rgb, len(recs)))
print(f"模板: {len(templates)} 张 (缺: {[c for c in lib_codes if c not in byc]})")

# ---------------- 交叉验证 (最终公式) ----------------
top1 = top3 = tot = 0
for code, recs in byc.items():
    if len(recs) < 2: continue
    for i, r in enumerate(recs):
        tpl = {}
        for c2, rs in byc.items():
            pool = [x for j, x in enumerate(rs) if not (c2 == code and j == i)]
            if pool:
                m = [sum(x[2][ch] for x in pool)/len(pool) for ch in range(10)]
                t = spec8(m)
                if t: tpl[c2] = t
        q = spec8(r[2])
        if not q: continue
        sc = sorted((math.sqrt(sum((q[k]-tpl[c][k])**2 for k in range(8))), c) for c in tpl)
        tot += 1; top1 += sc[0][1] == code; top3 += code in [c for _, c in sc[:3]]
acc = f"top-1 {top1}/{tot} = {top1/max(tot,1)*100:.1f}% | top-3 {top3}/{tot} = {top3/max(tot,1)*100:.1f}%"
print("留一交叉验证:", acc)

# ---------------- 生成 ESP32 头文件 ----------------
os.makedirs(os.path.dirname(OUT_H), exist_ok=True)
with open(OUT_H, "w", encoding="utf-8") as f:
    f.write("""// ============================================================
// SkinPen 皮肤色号光谱模板 — 自动生成, 勿手改
// 生成脚本: 皮肤数据/build_final.py
// 数据: records.csv 678 条 -> 清洗(cv_max<0.05+物理边界+卡内离群)
//       -> 有效 %d 条, %d 张色卡模板
// 公式: spec = sqrt(f1..f8) 归一化(和=1), 欧氏距离最近邻
//       留一交叉验证 %s
// RGB : PANTONE SkinTone 官方色 (AdobeRGB1998 -> sRGB -> RGB565)
// ============================================================
#pragma once
#include <stdint.h>

#define SKIN_TPL_COUNT %d

typedef struct {
    int      seq;        // 色卡序号
    char     code[8];    // 色号 "3Y06SP"
    float    spec[8];   // sqrt(f1..f8) 归一化模板
    uint16_t rgb565;     // 官方色 (sRGB->RGB565)
} skin_tpl_t;

static const skin_tpl_t g_skin_tpl[SKIN_TPL_COUNT] = {
""" % (len(final), len(templates), acc, len(templates)))
    for seq, code, sp, r565, rgb, n in templates:
        vals = ", ".join(f"{x:.5f}f" for x in sp)
        f.write(f"    {{ {seq}, \"{code}\", {{{vals}}}, 0x{r565:04X} }},"
                f"  // sRGB({rgb[0]},{rgb[1]},{rgb[2]}) n={n}\n")
    f.write("};\n")
print(f"\n已生成: {OUT_H}")

# ---------------- 报告 ----------------
with open(OUT_TXT, "w", encoding="utf-8") as f:
    f.write(f"""SkinPen 最终资产报告
====================
清洗: 678 -> cv/物理边界 {len(stage)} -> 卡内离群 -> {len(final)} 条
模板: {len(templates)} 张 (4Y14SP 无测量数据, 不入库)
RGB 对齐: 库无RGB {missing_rgb}, 表多余 {extra_rgb}
最终公式: sqrt(f1..f8) 归一化 + 欧氏最近邻
留一交叉验证: {acc}
输出: skinpen_v2/main/skin_templates.h
""")
print(f"报告: {OUT_TXT}")
