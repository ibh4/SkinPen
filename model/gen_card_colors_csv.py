#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 139 卡颜色表 CSV: seq, code, name_en, adobe_r/g/b, srgb_r/g/b, hex, has_data"""
import csv, os

BASE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(BASE, "皮肤光谱数据_extracted/皮肤光谱数据/CONFIG/pantone_skintone.csv")
OUT = "/Users/pwngwc/projects/ESP32/SkinPen-色卡颜色表-139.csv"

# 与 build_training_csv.py / train_color_model.py 的 P 表同源 (AdobeRGB 1998)
import importlib.util
spec = importlib.util.spec_from_file_location("btc", os.path.join(BASE, "build_training_csv.py"))
btc = importlib.util.module_from_spec(spec)
import sys
_p = sys.stdout
sys.stdout = open(os.devnull, "w")
try:
    spec.loader.exec_module(btc)
except SystemExit:
    pass
finally:
    sys.stdout = _p
P, adobe_to_srgb = btc.P, btc.adobe_to_srgb

rows = list(csv.DictReader(open(SRC, encoding="utf-8-sig")))
with open(OUT, "w", newline="", encoding="utf-8-sig") as fp:
    w = csv.writer(fp)
    w.writerow(["seq", "code", "name_en", "adobe_r", "adobe_g", "adobe_b",
                "srgb_r", "srgb_g", "srgb_b", "hex", "has_data"])
    for r in sorted(rows, key=lambda x: int(x["seq"])):
        code = r["code"]
        if code in P:
            ad = P[code]
            s = adobe_to_srgb(ad)
            w.writerow([r["seq"], code, r["name_en"], *ad, *s,
                        f"#{s[0]:02X}{s[1]:02X}{s[2]:02X}", 1])
        else:
            w.writerow([r["seq"], code, r["name_en"], "", "", "", "", "", "", "", 0])
print(f"OK {len(rows)} rows -> {OUT} (含色 {sum(1 for r in rows if r['code'] in P)})")
