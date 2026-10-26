#!/usr/bin/env python3
# model.c host 端对拍验证 —— SkinPen V0.5
# 1) 从 color_model.json 取若干真实色卡 spec, 重构原始 mean[10](模拟 AS7341 读数取整)
# 2) numpy 参考: spec8+lnsum 特征 -> JSON 权重推理(与 JS predictModel 逐位一致)
# 3) gcc 编译 main/model.c + 测试入口, 运行对比 RGB/冷暖/肤深
import json, math, struct, subprocess, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))          # 工程根
MODEL_JSON = os.path.join(os.path.dirname(os.path.dirname(ROOT)), "皮肤数据/analysis_out/color_model.json")
MAIN = os.path.join(ROOT, "main")

with open(MODEL_JSON, "r", encoding="utf-8") as f:
    M = json.load(f)

xm, xs = M["x_mean"], M["x_scale"]
W1 = M["layers"][0]["W"]; b1 = M["layers"][0]["b"]   # W1[9][20]
W2 = M["layers"][1]["W"]; b2 = M["layers"][1]["b"]   # W2[20][5]

def predict(spec):
    """numpy-free 参考实现, 输入 spec9(spec8+lnsum), 与 JS predictModel 一致"""
    x = [(spec[i] - xm[i]) / xs[i] for i in range(9)]
    h = []
    for j in range(20):
        s = b1[j]
        for i in range(9):
            s += x[i] * W1[i][j]
        h.append(max(s, 0.0))
    y = []
    for k in range(5):
        s = b2[k]
        for j in range(20):
            s += h[j] * W2[j][k]
        y.append(s)
    rgb = []
    for k in range(3):
        v = min(max(y[k], 0.0), 1.0)
        rgb.append(min(max(int(v * 255.0 + 0.5), 0), 255))
    cool = y[3] > 0.5
    dep = min(max(int(y[4] * 104.0 + 0.5), 0), 104)
    return rgb, cool, dep

def spec_to_mean(spec):
    """spec8+lnsum -> 重构原始 mean[10] (f=(s*T)^2, T^2*sum(s^2)=e^L), 四舍五入取整"""
    L = spec[8]
    s2 = sum(v * v for v in spec[:8])
    T = math.sqrt(math.exp(L) / s2)
    fs = [round((spec[i] * T) ** 2) for i in range(8)]
    return fs

# ---- 选取测试卡: 均匀取样 24 张 + 两端 ----
cards = M["cards"]
idxs = sorted(set([0, 1, len(cards) // 6, len(cards) // 3, len(cards) // 2,
                   len(cards) * 2 // 3, len(cards) * 5 // 6, len(cards) - 1] +
                  [len(cards) * k // 24 for k in range(1, 24)]))
cases = []
for i in idxs:
    c = cards[i]
    fs = spec_to_mean(c["spec"])
    cases.append((c["code"], fs))
cases.append(("ZEROS", [0] * 8))            # 边界: 全零 -> model_predict 返回 -1
cases.append(("TINY", [1, 1, 1, 1, 1, 1, 1, 1]))

# ---- 生成 C 测试文件 ----
lines = []
lines.append('#include "model.h"')
lines.append('#include <stdio.h>')
lines.append('#include <stdlib.h>')
lines.append('#include <string.h>')
lines.append('typedef struct { const char *code; uint16_t mean[10]; int er,eg,eb; int edep; int ecool; int expect_fail; } case_t;')
lines.append('static const case_t CASES[] = {')
for code, fs in cases:
    if code == "ZEROS":
        lines.append('  {"ZEROS",{0,0,0,0,0,0,0,0,0,0},0,0,0,0,0,1},')
        continue
    # 重构回 spec 后预测(取整误差存在, 允许 ±1)
    s8 = [math.sqrt(v) for v in fs]
    t = sum(s8)
    spec = [v / t for v in s8] + [math.log(sum(fs))]
    rgb, cool, dep = predict(spec)
    mean10 = fs + [0, 0]
    mean_s = ",".join(str(v) for v in mean10)
    lines.append('  {"%s",{%s},%d,%d,%d,%d,%d,0},' % (code, mean_s, rgb[0], rgb[1], rgb[2], dep, 1 if cool else 0))
lines.append('};')
lines.append('''
int main(void) {
    int n = sizeof(CASES) / sizeof(CASES[0]), fail = 0;
    for (int i = 0; i < n; i++) {
        const case_t *c = &CASES[i];
        model_out_t o;
        int rc = model_predict(c->mean, &o);
        if (c->expect_fail) {
            if (rc == 0) { printf("FAIL %s: 预期失败但成功\\n", c->code); fail++; }
            else printf("PASS %s (rc=%d)\\n", c->code, rc);
            continue;
        }
        if (rc != 0) { printf("FAIL %s: rc=%d\\n", c->code, rc); fail++; continue; }
        int dr = abs((int)o.r - c->er), dg = abs((int)o.g - c->eg), db = abs((int)o.b - c->eb);
        int dd = abs(o.dep16 - c->edep);
        int ec = o.cool ? 1 : 0;
        if (dr > 1 || dg > 1 || db > 1 || dd > 1 || ec != c->ecool) {
            printf("FAIL %s: C=(%d,%d,%d,%s,%d) ref=(%d,%d,%d,%d,%d)\\n",
                   c->code, o.r, o.g, o.b, o.cool ? "C" : "W", o.dep16,
                   c->er, c->eg, c->eb, c->ecool, c->edep);
            fail++;
        } else {
            printf("PASS %s: (%d,%d,%d) %s dep=%d\\n",
                   c->code, o.r, o.g, o.b, o.cool ? "C" : "W", o.dep16);
        }
    }
    printf("----\\n%d/%d passed\\n", n - fail, n);
    return fail ? 1 : 0;
}''')

test_c = os.path.join(ROOT, "tools", "test_model_main.c")
with open(test_c, "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

# ---- gcc 编译运行 ----
bin_path = "/tmp/skinpen_model_test"
cc = ["gcc", "-I", MAIN, "-O2", "-o", bin_path, test_c, os.path.join(MAIN, "model.c")]
r = subprocess.run(cc, capture_output=True, text=True)
if r.returncode != 0:
    print("编译失败:\n" + r.stderr)
    sys.exit(1)
r = subprocess.run([bin_path], capture_output=True, text=True)
print(r.stdout)
if r.returncode != 0:
    print("对拍未通过", file=sys.stderr)
    sys.exit(1)
print("model.c 与 Python 参考实现对拍通过")
