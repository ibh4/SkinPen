#!/usr/bin/env python3
# 生成固件内置色卡库 main/pantone_builtin.c
# 数据源: tools/pantone_skintone.csv (139 条 PANTONE SkinTone)
# 用途: TF 卡 /config/pantone_skintone.csv 缺失时的回退(文件存在时仍优先)
# 重新生成: python3 tools/gen_pantone_builtin.py
import os, csv, io

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "pantone_skintone.csv")
OUT = os.path.join(HERE, "..", "main", "pantone_builtin.c")

with open(SRC, "r", encoding="utf-8-sig") as fp:   # utf-8-sig 自动剥 BOM
    rows = list(csv.DictReader(fp))

cards = []
for r in rows:
    if r.get("enabled", "1").strip() == "0":
        continue
    seq = int(r["seq"].strip())
    code = r["code"].strip()
    name = r.get("name_en", "").strip()
    cards.append((seq, code, name))

cards.sort(key=lambda x: x[0])
assert len(cards) == len(set(s for s, _, _ in cards)), "重复 seq"
assert len(set(c for _, c, _ in cards)) == len(cards), "重复 code"

lines = []
lines.append("// 固件内置 PANTONE SkinTone 色卡库(自动生成,勿手改)")
lines.append("// 生成: tools/gen_pantone_builtin.py <- tools/pantone_skintone.csv")
lines.append(f"// 条数: {len(cards)} (与实体色卡一一对应, seq 升序)")
lines.append('#include "pantone.h"')
lines.append("")
lines.append("const card_record_t PANTONE_BUILTIN[] = {")
for seq, code, name in cards:
    lines.append(f'    {{ {seq}, "{code}", "{name}", true }},')
lines.append("};")
lines.append("")
lines.append("const int PANTONE_BUILTIN_COUNT = "
             "sizeof(PANTONE_BUILTIN) / sizeof(PANTONE_BUILTIN[0]);")
lines.append("")

with open(OUT, "w", encoding="utf-8") as fp:
    fp.write("\n".join(lines))
print(f"OK: {len(cards)} cards -> {os.path.normpath(OUT)}")
