#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成训练数据 CSV(与 train_color_model.py 清洗规则逐位一致)
输出: /Users/pwngwc/projects/ESP32/SkinPen-训练数据-440条.csv
列: seq, code, f1..f8, clear, nir, cv_max, srgb_r/g/b, cool, dep16, spec8_1..8, lnsum
"""
import csv, math, os
from collections import defaultdict

BASE = os.path.dirname(os.path.abspath(__file__))
CSV_PATH = os.path.join(BASE, "皮肤光谱数据_extracted/皮肤光谱数据/DATA/20260101/records.csv")
OUT = "/Users/pwngwc/projects/ESP32/SkinPen-训练数据-440条.csv"
CV_THR, SUM_MIN, CLEAR_MIN, CARD_THR = 0.05, 200, 80, 0.10

# 138 卡官方 RGB (AdobeRGB 1998, photoshoplus) —— 与 train_color_model.py/build_final.py 一致
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
"4Y09SP":(164,131,101),"4Y10SP":(154,122,92),"4Y11SP":(143,112,84),"4Y12SP":(131,100,73),
"5R04SP":(198,160,152),"5R05SP":(195,153,144),"5R06SP":(193,149,140),"5R07SP":(190,145,134),
"5Y01SP":(201,173,149),"5Y02SP":(197,169,145),"5Y03SP":(196,168,144),"5Y04SP":(192,164,141),
"5Y05SP":(189,159,135),"5Y06SP":(184,154,126),"5Y07SP":(177,145,117),"5Y08SP":(170,139,109),
"5Y09SP":(163,130,100),"5Y10SP":(155,125,93),
"6Y01SP":(203,176,148),"6Y02SP":(200,173,145),"6Y03SP":(194,168,142),"6Y04SP":(187,163,140),
}
assert len(P) == 138

def adobe_to_srgb(ad):
    M = ((1.39840, -0.39854,  0.00001),
         (0.00012,  1.00006, -0.00004),
         (0.00004, -0.04294,  1.04316))
    lin = [(c/255.0) ** 2.19921875 for c in ad]
    s = [sum(M[i][j]*lin[j] for j in range(3)) for i in range(3)]
    out = []
    for v in s:
        v = max(0.0, min(1.0, v))
        out.append(int(round((1.055*v**(1/2.4)-0.055)*255)) if v > 0.0031308 else int(round(12.92*v*255)))
    return out

def code_meta(code):
    dep = int(code[0]) if code[0] in "123456" else 3
    level = 8
    if len(code) >= 4 and code[2].isdigit() and code[3].isdigit():
        level = min(int(code[2:4]), 15)
    return (1 if code[1] == 'R' else 0), (dep - 1) * 16 + level + 8

CH = ["f1","f2","f3","f4","f5","f6","f7","f8","clear","nir"]

def rdist(a, b):
    nb = math.sqrt(sum(x*x for x in b))
    return math.sqrt(sum((a[i]-b[i])**2 for i in range(len(a))))/nb if nb > 1e-9 else float('inf')

rows = list(csv.DictReader(open(CSV_PATH, encoding="utf-8")))
stage = []
for r in rows:
    v = [float(r[c]) for c in CH]
    if float(r["cv_max"]) < CV_THR and sum(v) >= SUM_MIN and v[8] >= CLEAR_MIN:
        stage.append((r["timestamp"], int(r["seq"]), r["code"], v, float(r["cv_max"])))
by_card = defaultdict(list)
for rec in stage:
    by_card[rec[2]].append(rec)
final = []
for code, recs in by_card.items():
    if len(recs) <= 2:
        final.extend(recs); continue
    med = [sum(x[3][ch] for x in recs)/len(recs) for ch in range(10)]
    good = [r for r in recs if rdist(r[3], med) <= CARD_THR] or [min(recs, key=lambda r: rdist(r[3], med))]
    final.extend(good)
print(f"清洗: {len(rows)} -> {len(stage)} -> {len(final)} 条, {len(by_card)} 卡")

final.sort(key=lambda r: (r[1], r[0]))
with open(OUT, "w", newline="", encoding="utf-8-sig") as fp:
    w = csv.writer(fp)
    w.writerow(["seq","code","timestamp","f1","f2","f3","f4","f5","f6","f7","f8","clear","nir","cv_max",
                "srgb_r","srgb_g","srgb_b","cool","dep16",
                "spec8_1","spec8_2","spec8_3","spec8_4","spec8_5","spec8_6","spec8_7","spec8_8","lnsum"])
    for ts, seq, code, v, cv in final:
        srgb = adobe_to_srgb(P[code])
        cool, dep16 = code_meta(code)
        s = [math.sqrt(max(x, 0.0)) for x in v[:8]]
        t = sum(s)
        spec = [x/t for x in s] if t > 0 else [1/8]*8
        w.writerow([seq, code, ts] + [int(x) for x in v[:8]] + [int(v[8]), int(v[9]), round(cv, 4),
                    srgb[0], srgb[1], srgb[2], cool, dep16] + [round(x, 6) for x in spec] + [round(math.log(max(sum(v[:8]), 1.0)), 6)])
print(f"OK -> {OUT}")
