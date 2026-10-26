#!/usr/bin/env python3
# color_model.json -> model_weights.h (float32 字面量, 与 JSON 同源)
import json, os, struct

src = "/Users/pwngwc/projects/ESP32/皮肤数据/analysis_out/color_model.json"
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "main", "model_weights.h")
m = json.load(open(src))

def f32(v):
    # float32 精确表示
    f = struct.unpack("f", struct.pack("f", float(v)))[0]
    return repr(f)

def arr(vals, indent):
    return (",\n" + " " * indent).join(f32(v) for v in vals)

L = []
L.append("// 由 皮肤数据/analysis_out/color_model.json 自动生成 —— 请勿手改")
L.append("// 模型: %s  特征: %s  输出: %s" % (m["name"], m["feature"], ", ".join(m["outputs"])))
L.append("#pragma once")
L.append("")
L.append("#define MODEL_IN_DIM   9")
L.append("#define MODEL_HID_DIM  20")
L.append("#define MODEL_OUT_DIM  5")
L.append("")
L.append("// 标准化参数 (9 维: spec8 + ln(sum))")
L.append("static const float MODEL_X_MEAN[MODEL_IN_DIM] = {")
L.append(arr(m["x_mean"], 4))
L.append("};")
L.append("static const float MODEL_X_SCALE[MODEL_IN_DIM] = {")
L.append(arr(m["x_scale"], 4))
L.append("};")
L.append("")
W1, b1 = m["layers"][0]["W"], m["layers"][0]["b"]
L.append("// 隐层 W1[9][20] (行=输入, 列=隐单元) + b1[20], ReLU")
L.append("static const float MODEL_W1[MODEL_IN_DIM][MODEL_HID_DIM] = {")
rows = []
for r in W1:
    rows.append("    { " + (",\n      ").join(f32(v) for v in r) + " }")
L.append(",\n".join(rows))
L.append("};")
L.append("static const float MODEL_B1[MODEL_HID_DIM] = {")
L.append(arr(b1, 4))
L.append("};")
L.append("")
W2, b2 = m["layers"][1]["W"], m["layers"][1]["b"]
L.append("// 输出层 W2[20][5] (行=隐单元, 列=输出), 线性")
L.append("// 输出: [0]=R [1]=G [2]=B (0..1) [3]=冷暖得分(>0.5 冷粉C) [4]=肤深/104")
L.append("static const float MODEL_W2[MODEL_HID_DIM][MODEL_OUT_DIM] = {")
rows = []
for r in W2:
    rows.append("    { " + (",\n      ").join(f32(v) for v in r) + " }")
L.append(",\n".join(rows))
L.append("};")
L.append("static const float MODEL_B2[MODEL_OUT_DIM] = {")
L.append(arr(b2, 4))
L.append("}")
open(dst, "w").write("\n".join(L) + "\n")
print("wrote", dst)
