#!/usr/bin/env python3
# 生成 PANTONE SkinTone 色卡库 CSV (138 条)
# 输出: tools/pantone_skintone.csv
# 使用: 拷贝到 TF 卡 /config/pantone_skintone.csv (FAT32)
# 格式: seq,code,name_en,enabled

TOTAL = 138

# 肤色深度族: 数字越大肤色越深;R=红调, Y=黄调
FAMILIES = [
    ("1R", 18), ("2R", 18), ("3R", 18), ("4R", 15),   # 69
    ("1Y", 18), ("2Y", 18), ("3Y", 18), ("4Y", 15),   # 69
]

def main():
    rows = []
    seq = 0
    for fam, count in FAMILIES:
        for shade in range(count):
            seq += 1
            code = f"{fam}{shade:02d}SP"
            name = f"PANTONE {fam}{shade:02d} SP"
            rows.append((seq, code, name, 1))
    assert len(rows) == TOTAL, f"条目数 {len(rows)} != {TOTAL}"

    out = __file__.rsplit("/", 1)[0] + "/color_cards.csv"
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("seq,code,name_en,enabled\n")
        for seq, code, name, en in rows:
            f.write(f"{seq},{code},{name},{en}\n")
    print(f"OK: {len(rows)} -> {out}")

if __name__ == "__main__":
    main()
