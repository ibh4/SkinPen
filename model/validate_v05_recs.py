#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
V0.5 模型 vs 官方色: 推荐结果一致性验证
=======================================
背景(此前 metric_experiment 结论): NN 查表 top-1 91.4%, 误差全部落在光谱
几乎重合的相邻色号间(如 3Y05SP vs 4Y05SP 距离 0.0011), 是 10 通道物理极限.
V0.5 不再输出色号, 只输出颜色+推荐 —— 所以真正要验证的是:
  用模型预测色算推荐, 与用官方色(查表版)算推荐, 结果差多少?
一致率过低才说明模型误差影响业务; 接近 100% 则物理极限对 V0.5 无实际影响.
"""
import json, math

MJ = "/Users/pwngwc/projects/ESP32/皮肤数据/analysis_out/color_model.json"
M = json.load(open(MJ))

# ---- 固件同款数学 (recommend.c / gui_preview.html 逐位一致) ----
def rgb565(r, g, b):
    return (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)) & 0xFFFF

def un565(c):
    r5, g6, b5 = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))

def lum565(c):
    r, g, b = un565(c)
    return (299 * r + 587 * g + 114 * b) // 1000

def dist2(c5, rgb):
    cr, cg, cb = un565(c5)
    return sum((a - b) ** 2 for a, b in zip((cr, cg, cb), rgb))

F565 = lambda h: rgb565(int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
FND_C = [(f"{i+1}C", F565(h)) for i, h in enumerate(
    ["F3DED2","EDCFBF","E4BDA6","D9AB92","CB967A","B97F60","A4694C","8B553C","6D4030"])]
FND_W = [(f"{i+1}W", F565(h)) for i, h in enumerate(
    ["F4DEBC","ECD0A6","E2BD8E","D6A877","C69261","B47C4E","9D663E","825231","643D26"])]
LIP = [(c, F565(h), w, cl, dmin, dmax) for c, h, w, cl, dmin, dmax in [
    ("202","A97A68",1,1,1,2),("213","E26A80",0,1,1,2),("215","C02938",0,1,5,6),
    ("217","B56D4C",1,0,2,2),("233","B66A74",1,1,2,3),("239","CF5A3A",1,0,3,3),
    ("300","B04F3E",1,1,5,5),("302","A34A2E",1,0,6,6),("Sheer","EF9D8D",1,1,1,1),
    ("Chili","B9542F",1,0,4,5),("Ruby","C23B48",0,1,4,4),("Teddy","B06F5F",1,1,2,3),
    ("Marra","A04F33",1,0,5,6),("999","C62828",1,1,3,4),("772","C47680",0,1,2,2),
    ("405","D15A3A",1,0,2,3),("TF16","B22226",1,1,4,5),("1966","92342C",1,0,5,6)]]

def rec_v04(skin565, code):
    """V0.4 查表版推荐: 底调/肤深来自色号 (作为基准真值)"""
    ut = code[1] if len(code) > 1 and code[1] in "RY" else 'Y'
    dep = int(code[0]) if code[0] in "123456" else 3
    level = 8
    if len(code) >= 4 and code[2].isdigit() and code[3].isdigit():
        level = min(int(code[2:4]), 15)
    pool = FND_C if ut == 'R' else FND_W
    r, g, b = un565(skin565)
    best = min(range(9), key=lambda i: dist2(pool[i][1], (r, g, b)))
    lo, hi = max(best - 1, 0), min(best + 1, 8)
    dep16 = (dep - 1) * 16 + level + 8
    target = lum565(skin565) - 25
    cand = [l for l in LIP if (l[3] if ut == 'R' else l[2])]
    inr = [l for l in cand if l[4] * 16 - 8 <= dep16 <= l[5] * 16 + 8]
    p = inr or cand
    li = min(p, key=lambda l: abs(lum565(l[1]) - target))
    if abs(lum565(li[1]) - target) > 40:
        li = min(cand, key=lambda l: abs(lum565(l[1]) - target))
    return pool[best][0], li[0]

def rec_v05(skin565, cool, dep16):
    """V0.5 模型版推荐: 冷暖/肤深由模型输出, 推荐算法与 V0.4 完全一致"""
    r, g, b = un565(skin565)
    pool = FND_C if cool else FND_W
    best = min(range(9), key=lambda i: dist2(pool[i][1], (r, g, b)))
    lo, hi = max(best - 1, 0), min(best + 1, 8)
    target = lum565(skin565) - 25
    cand = [l for l in LIP if (l[3] if cool else l[2])]
    inr = [l for l in cand if l[4] * 16 - 8 <= dep16 <= l[5] * 16 + 8]
    p = inr or cand
    li = min(p, key=lambda l: abs(lum565(l[1]) - target))
    if abs(lum565(li[1]) - target) > 40:
        li = min(cand, key=lambda l: abs(lum565(l[1]) - target))
    return pool[best][0], li[0]

# ---- 模型推理 (与固件将来 model.c 一致): 5 输出 rgb+cool+dep16 ----
def predict(spec):
    x = [(v - m) / s for v, m, s in zip(spec, M["x_mean"], M["x_scale"])]
    for li, layer in enumerate(M["layers"]):
        W, b = layer["W"], layer["b"]
        out = []
        for j in range(len(b)):
            s = b[j] + sum(x[i] * W[i][j] for i in range(len(x)))
            out.append(max(0.0, s) if li < len(M["layers"]) - 1 else s)
        x = out
    return x   # [r,g,b] 0..1 + cool + dep16/104

# ---- 逐卡对比 ----
fnd_ok = lip_ok = ut_ok = 0
n = 0
worst = []
fnd_cat = {"adjacent": 0, "cross_pool": 0, "far": 0}   # ±1档(3格覆盖) / 跨池 / 同池≥2档
lip_cat = {"overlap": 0, "apart": 0}                    # 适用肤深区间有重叠 / 完全不重叠
def lip_rng(c):
    l = next(x for x in LIP if x[0] == c)
    return l[4], l[5]
for card in M["cards"]:
    code, srgb, spec = card["code"], card["srgb"], card["spec"]
    off5 = rgb565(*srgb)
    out = predict(spec)
    pred = [max(0, min(255, round(v * 255))) for v in out[:3]]
    pr5 = rgb565(*pred)
    cool_est = out[3] > 0.5
    dep16_est = out[4] * 104.0
    ut_ok += cool_est == (code[1] == 'R')
    fnd_t, lip_t = rec_v04(off5, code)      # 官方色+色号 (基准)
    fnd_m, lip_m = rec_v05(pr5, cool_est, dep16_est)  # 模型预测色+元数据 (V0.5)
    n += 1
    fnd_ok += fnd_t == fnd_m
    lip_ok += lip_t == lip_m
    if fnd_t != fnd_m:
        if fnd_t[1] != fnd_m[1]:
            fnd_cat["cross_pool"] += 1
        elif abs(int(fnd_t[0]) - int(fnd_m[0])) == 1:
            fnd_cat["adjacent"] += 1
        else:
            fnd_cat["far"] += 1
    if lip_t != lip_m:
        a, b = lip_rng(lip_t), lip_rng(lip_m)
        lip_cat["overlap" if a[0] <= b[1] and b[0] <= a[1] else "apart"] += 1
    if fnd_t != fnd_m or lip_t != lip_m:
        worst.append((code, fnd_t, fnd_m, lip_t, lip_m))

print(f"138 卡推荐一致性 (V0.5 模型预测色 vs 官方色查表):")
print(f"  冷暖判定一致: {ut_ok}/{n} = {ut_ok/n*100:.1f}%")
print(f"  粉底主推一致: {fnd_ok}/{n} = {fnd_ok/n*100:.1f}%")
print(f"  口红推荐一致: {lip_ok}/{n} = {lip_ok/n*100:.1f}%")
print(f"\n粉底不一致 {n-fnd_ok} 例分类: 同池相邻档±1(屏显3格覆盖)={fnd_cat['adjacent']}, "
      f"跨池={fnd_cat['cross_pool']}, 同池≥2档={fnd_cat['far']}")
print(f"口红不一致 {n-lip_ok} 例分类: 适用肤深区间重叠={lip_cat['overlap']}, 完全不重叠={lip_cat['apart']}")
print(f"\n不一致明细 (色号, 基准粉底->模型粉底, 基准口红->模型口红):")
for w in worst:
    print(f"  {w[0]:<9} {w[1]}->{w[2]:<3} {w[3]:<6}->{w[4]}")
