#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SkinPen V0.5 颜色回归模型训练
==============================
替代 V0.4 的"色号建模板 + 最近邻查表"方案:
  光谱 f1..f8 (sqrt 归一化) --回归模型--> 皮肤颜色 (直接预测, 不经色号)
模型需可移植到 ESP32 (纯 C 点积/矩阵乘), 候选:
  - ridge 多项式特征线性回归
  - 小型 MLP (8->16->3 / 8->24->12->3, relu/tanh)
评估: 5 折 CV, CIE76 dE 与 RGB MAE; 基线 = V0.4 最近邻模板
输出: analysis_out/color_model.json (网页演示 + 固件权重同源)
"""
import csv, json, math, os
from collections import defaultdict

import numpy as np
from sklearn.linear_model import Ridge
from sklearn.neural_network import MLPRegressor
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import KFold
from sklearn.metrics import mean_absolute_error

BASE = "/Users/pwngwc/projects/ESP32/皮肤数据"
CSV_PATH = os.path.join(BASE, "皮肤光谱数据_extracted/皮肤光谱数据/DATA/20260101/records.csv")
OUT_JSON = os.path.join(BASE, "analysis_out", "color_model.json")

CV_THR, SUM_MIN, CLEAR_MIN, CARD_THR = 0.05, 200, 80, 0.10

# ---------------- 官方 RGB 表 (与 build_final.py 完全一致) ----------------
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

# ---------------- 数据加载 + 清洗 (与 build_final.py 一致) ----------------
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
for rec in stage:
    by_card[rec[1]].append(rec)
final = []
for code, recs in by_card.items():
    if len(recs) <= 2:
        final.extend(recs); continue
    med = [sum(x[2][ch] for x in recs)/len(recs) for ch in range(10)]
    good = [r for r in recs if rdist(r[2], med) <= CARD_THR] or [min(recs, key=lambda r: rdist(r[2], med))]
    final.extend(good)
print(f"清洗: {len(rows)} -> {len(stage)} -> {len(final)} 条, {len(by_card)} 卡")

# ---------------- 特征 / 目标 ----------------
def spec8(v):
    s = np.sqrt(np.maximum(np.asarray(v[:8], float), 0.0))
    t = s.sum()
    return s / t if t > 0 else np.full(8, 1/8)

X8, X8B, Y_RGB, META = [], [], [], []
for seq, code, v in final:
    sp = spec8(v)
    X8.append(sp)
    X8B.append(np.concatenate([sp, [math.log(max(sum(v[:8]), 1.0))]]))
    Y_RGB.append(np.array(adobe_to_srgb(P[code]), float)/255.0)
    META.append((seq, code))
X8, X8B, Y_RGB = np.array(X8), np.array(X8B), np.array(Y_RGB)
print("特征:", X8.shape, X8B.shape, "目标:", Y_RGB.shape)

# 多任务目标: 冷暖底调(R=1) + 连续肤深 dep16=(dep-1)*16+level+8 (0~104)
# 推荐算法需要这两个元数据; 直接由模型输出, 推荐逻辑与 V0.4 完全一致
def code_meta(code):
    dep = int(code[0]) if code[0] in "123456" else 3
    level = 8
    if len(code) >= 4 and code[2].isdigit() and code[3].isdigit():
        level = min(int(code[2:4]), 15)
    return (1.0 if code[1] == 'R' else 0.0), (dep - 1) * 16 + level + 8

Y_COOL = np.array([code_meta(c)[0] for _, c in META])
Y_DEP16 = np.array([code_meta(c)[1] for _, c in META], float)
Y_MT = np.hstack([Y_RGB, Y_COOL[:, None], (Y_DEP16 / 104.0)[:, None]])   # 5 输出
print("多任务目标:", Y_MT.shape, "(rgb3 + 冷暖1 + dep16/104 1)")

# ---------------- 颜色空间工具 ----------------
def srgb_to_lab(c):  # c: N x 3, 0..1
    def lin(x):
        return np.where(x <= 0.04045, x/12.92, ((x+0.055)/1.055)**2.4)
    cc = lin(np.clip(c, 0, 1))
    M = np.array([[0.4124564,0.3575761,0.1804375],
                  [0.2126729,0.7151522,0.0721750],
                  [0.0193339,0.1191920,0.9503041]])
    xyz = cc @ M.T
    xyz = xyz / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787*xyz + 16/116)
    L = 116*f[:,1] - 16
    a = 500*(f[:,0]-f[:,1]); b = 200*(f[:,1]-f[:,2])
    return np.stack([L,a,b], axis=1)

LAB = srgb_to_lab(Y_RGB)

def lab_to_srgb01(lab):  # lab: 长度3
    L, a, b = lab
    fy = (L+16)/116; fx = fy + a/500; fz = fy - b/200
    def finv(t):
        t3 = t**3
        return t3 if t3 > 0.008856 else (t - 16/116)/7.787
    xyz = np.array([finv(fx), finv(fy), finv(fz)]) * np.array([0.95047, 1.0, 1.08883])
    M = np.array([[ 3.2404542,-1.5371385,-0.4985314],
                  [-0.9692660, 1.8760108, 0.0415560],
                  [ 0.0556434,-0.2040259, 1.0572252]])
    lin = np.clip(M @ xyz, 0, 1)
    return np.array([12.92*c if c <= 0.0031308 else 1.055*c**(1/2.4)-0.055 for c in lin])

def de_stats(pred01):
    d = np.sqrt(((srgb_to_lab(pred01) - LAB)**2).sum(axis=1))
    rgb_mae = float(mean_absolute_error(Y_RGB*255, np.clip(pred01,0,1)*255))
    return float(d.mean()), float(np.percentile(d,95)), float(d.max()), rgb_mae

# ---------------- 模型候选 ----------------
def poly(X):
    n = X.shape[1]; cols = [X]
    for i in range(n):
        for j in range(i, n):
            cols.append((X[:,i]*X[:,j])[:,None])
    return np.hstack(cols)

def mk_mlp(h, act="relu"):
    return lambda: MLPRegressor(hidden_layer_sizes=h, activation=act, solver="lbfgs",
                                max_iter=8000, random_state=0)

CAND = {
    "ridge_poly/rgb":  (poly,  Y_RGB, lambda: Ridge(alpha=1.0)),
    "ridge_poly/lab":  (poly,  LAB,   lambda: Ridge(alpha=1.0)),
    "mlp16/rgb":       (None,  Y_RGB, mk_mlp((16,))),
    "mlp16/lab":       (None,  LAB,   mk_mlp((16,))),
    "mlp24_12/rgb":    (None,  Y_RGB, mk_mlp((24,12))),
    "mlp24_12/lab":    (None,  LAB,   mk_mlp((24,12))),
    "mlp16t/rgb":      (None,  Y_RGB, mk_mlp((16,), "tanh")),
    "mlpB16/rgb":      (None,  Y_RGB, mk_mlp((16,))),      # 特征加 ln(sum), X8B
    "mlpB24_12/rgb":   (None,  Y_RGB, mk_mlp((24,12))),
}

def cv_eval(Xf, target, factory, use_B=False):
    kf = KFold(n_splits=5, shuffle=True, random_state=42)
    pred = np.zeros_like(target)
    for tr, te in kf.split(Xf):
        sc = StandardScaler().fit(Xf[tr])
        m = factory()
        m.fit(sc.transform(Xf[tr]), target[tr])
        pred[te] = m.predict(sc.transform(Xf[te]))
    if target is LAB:
        pred = np.array([lab_to_srgb01(p) for p in np.clip(pred, 0, 100)])
    return de_stats(pred)

results = {}
for name, (feats, target, factory) in CAND.items():
    use_B = name.startswith("mlpB")
    Xf = X8B if use_B else (poly(X8) if feats is not None else X8)
    if not use_B and feats is not None:
        # ridge_poly 两种特征都试: 主表用 X8 poly
        pass
    results[name] = cv_eval(Xf, target, factory)

# ---- 多任务模型: RGB + 冷暖 + 肤深 (5 输出) ----
MT_CAND = {
    "mlpB16/mt5":    (mk_mlp((16,)),   True),
    "mlpB20/mt5":    (mk_mlp((20,)),   True),
    "mlpB24_12/mt5": (mk_mlp((24,12)), True),
}
mt_stats = {}

def cv_eval_mt(Xf, factory):
    kf = KFold(n_splits=5, shuffle=True, random_state=42)
    pred = np.zeros_like(Y_MT)
    for tr, te in kf.split(Xf):
        sc = StandardScaler().fit(Xf[tr])
        m = factory()
        m.fit(sc.transform(Xf[tr]), Y_MT[tr])
        pred[te] = m.predict(sc.transform(Xf[te]))
    de = de_stats(np.clip(pred[:, :3], 0, 1))
    ut_acc = float(((pred[:, 3] > 0.5) == (Y_COOL > 0.5)).mean())
    dep_mae = float(np.abs(pred[:, 4] * 104 - Y_DEP16).mean())
    return de, ut_acc, dep_mae

for name, (factory, useB) in MT_CAND.items():
    de, ut, dm = cv_eval_mt(X8B if useB else X8, factory)
    results[name] = de
    mt_stats[name] = (ut, dm)

# ---------------- V0.4 最近邻基线 (留一) ----------------
rec_by_card = defaultdict(list)
for i, (seq, code) in enumerate(META):
    rec_by_card[code].append(i)
pred_nn = []
for i, (seq, code) in enumerate(META):
    best, bd = None, 1e9
    for c, idxs in rec_by_card.items():
        pool = [j for j in idxs if j != i]
        t = X8[pool].mean(axis=0) if pool else X8[idxs].mean(axis=0)
        d = float(np.sqrt(((t - X8[i])**2).sum()))
        if d < bd: bd, best = d, c
    pred_nn.append(np.array(adobe_to_srgb(P[best]), float)/255.0)
pred_nn = np.array(pred_nn)
nn_stats = de_stats(pred_nn)

# ---------------- 汇总 ----------------
order = sorted(results.items(), key=lambda kv: kv[1][0])
print(f"\n{'模型':<16} {'dE均值':>7} {'p95':>6} {'max':>6} {'RGB_MAE':>8}")
for name, (m, p95, mx, mae) in order:
    print(f"{name:<16} {m:7.2f} {p95:6.2f} {mx:6.2f} {mae:8.2f}")
print(f"{'V0.4最近邻基线':<13} {nn_stats[0]:7.2f} {nn_stats[1]:6.2f} {nn_stats[2]:6.2f} {nn_stats[3]:8.2f}")

# ---------------- 选最优(多任务) + 全量重训 + 导出 ----------------
# 推荐需要冷暖/肤深元数据 -> 选 5 输出多任务模型; RGB 精度与纯 rgb 模型相当
mt_names = [n for n, _ in order if n.endswith("/mt5")]
best_name = mt_names[0]                    # order 已按 dE 均值升序
bm, bp95, bmax, bmae = results[best_name]
ut_acc, dep_mae = mt_stats[best_name]
print(f"\n选中: {best_name}  dE均值={bm:.2f}  冷暖准确率={ut_acc*100:.1f}%  肤深MAE={dep_mae:.1f}")
X_all = X8B
sc = StandardScaler().fit(X_all)
model = MT_CAND[best_name][0]()
model.fit(sc.transform(X_all), Y_MT)

layers = []
if hasattr(model, "coefs_"):            # MLP
    for W, b in zip(model.coefs_, model.intercepts_):
        layers.append({"W": np.asarray(W).tolist(), "b": np.asarray(b).tolist()})
else:                                     # Ridge 线性
    layers.append({"W": np.asarray(model.coef_).tolist(),
                   "b": np.asarray(model.intercept_).tolist()})

# 每卡演示光谱: 最终清洗记录 raw f1..f8 均值 -> spec8 (+lnsum)
cards = []
for c, idxs in sorted(rec_by_card.items(), key=lambda kv: META[kv[1][0]][0]):
    raw = np.mean([final[j][2][:8] for j in idxs], axis=0)
    sp = spec8(raw)
    feat = sp.tolist() + [math.log(max(float(raw.sum()), 1.0))]   # spec8+lnsum, 9维
    cards.append({"seq": META[idxs[0]][0], "code": c, "spec": [round(x,6) for x in feat],
                  "srgb": adobe_to_srgb(P[c])})

out = {
    "name": best_name, "feature": "spec8+lnsum",
    "outputs": ["r", "g", "b", "cool", "dep16"],   # cool: R冷粉=1; dep16: 输出×104 还原
    "x_mean": [round(x,8) for x in sc.mean_], "x_scale": [round(x,8) for x in sc.scale_],
    "layers": layers,
    "cv": {"dE_mean": round(bm,2), "p95": round(bp95,2), "max": round(bmax,2),
           "rgb_mae": round(bmae,2), "ut_acc": round(ut_acc,4), "dep16_mae": round(dep_mae,2)},
    "baseline_nn": {"dE_mean": round(nn_stats[0],2), "p95": round(nn_stats[1],2), "max": round(nn_stats[2],2)},
    "cards": cards,
}
os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
json.dump(out, open(OUT_JSON, "w"), ensure_ascii=False)
n_params = sum(len(np.array(l["W"]).ravel()) + len(l["b"]) for l in layers)
print(f"导出: {OUT_JSON}  参数量={n_params}  卡片={len(cards)}")
