#!/usr/bin/env python3
# SkinPen V0.5 新 GUI 中文字库生成
# 1) 从 Hiragino Sans GB.ttc 提取 W3/W6 两款字体
# 2) 汇总新 GUI 全部文案 -> 去重得到汉字集
# 3) npx lv_font_conv 生成 14/16/18/20/22/24px 字库(含 ASCII 0x20-0x7E)
#    以及 font_zh_14b.c(W6 加粗, 仅"发现你的肤色"副标题前半句)
import subprocess, os

TTC = "/System/Library/Fonts/Hiragino Sans GB.ttc"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "main")
TMP = "/tmp"

# ---- 提取 ttc 中的 W3 / W6 ----
from fontTools.ttLib import TTCollection
coll = TTCollection(TTC)
found = {}
for f in coll.fonts:
    name = f["name"].getDebugName(4) or ""
    if "W3" in name and "w3" not in found: found["w3"] = f
    if "W6" in name and "w6" not in found: found["w6"] = f
assert "w3" in found and "w6" in found, f"faces: {list(found)}"
for k, f in found.items():
    f.save(f"{TMP}/hiragino_{k}.ttf")
    print(f"saved {TMP}/hiragino_{k}.ttf <- {f['name'].getDebugName(4)}")

# ---- 新 GUI 全部中文文案(含启动画面/商品名/品牌名) ----
STRINGS = """
端侧模型光谱测肤色
发现你的肤色 · 粉底与口红搭配
记录 工具 开始测色
对准洁净、干燥的皮肤 返回 开始采集
保持不动 正在收集颜色信息 取消
偏暖金调 偏冷粉调 采集稳定 保存失败 粉底 唇色 完成 口红 更多
粉底 · 相邻三色一起试 浅一档 主推 深一档 查看
欧莱雅 True Match 冷调池中颜色最接近 暖调池中颜色最接近
主推相邻档，供实物试色 实际效果请与下颌、颈部对照 收藏搭配 已收藏
口红 · 换一种感觉 欧莱雅纷泽 柔和配色 · 非上唇模拟 换一支 为什么
推荐依据 按底调、肤深范围筛选 再比较与肤色的明暗关系 口红明度贴近肤色明度
本次测量 部位 未指定 显示色值 保存 已保存 重测
测量记录 还没有记录 测量后可保存肤色与搭配 本次
工具 色卡采集 查看异常反馈 色卡异常 传感器异常
工程模式 · 色卡采集 已采集 未采集 上一张 色卡列表 下一张 返回工具 采集此卡
选择色卡 选择 上一页 下一页
本次采集未通过 信号波动较大 信号不足，请贴近后重试 传感器读数失败
保持探头位置后重试 不生成新的肤色与推荐 重新测量
皮肤色号检测笔 粉底口红推荐
摩卡奶茶 亮丽粉 蓝调正红 肉桂色 玫瑰豆沙 辣椒红 红棕色 秋日落叶
浅珊瑚 小辣椒 裸棕 正红 干玫瑰 烂番茄 斯嘉丽红 复古红棕
阿玛尼红管 小金条
"""

chars = sorted(set(c for c in STRINGS if ord(c) > 0x7E))
symbols = "".join(chars)
print(f"unique non-ascii chars: {len(chars)}")
with open(f"{TMP}/skinpen_symbols.txt", "w") as fp:
    fp.write(symbols)

def gen(size, weight, out, extra=None):
    sym = symbols if extra is None else extra
    cmd = ["npx", "--no-install", "lv_font_conv",
           "--font", f"{TMP}/hiragino_{weight}.ttf",
           "--size", str(size), "--bpp", "4", "--format", "lvgl",
           "--no-compress", "--no-prefilter",
           "--range", "0x20-0x7E",
           "--symbols", sym,
           "--force-fast-kern-format",
           "-o", out]
    subprocess.run(cmd, check=True)
    print("gen", out)

for s in (14, 16, 18, 20, 22, 24):
    gen(s, "w3", os.path.join(OUT, f"font_zh_{s}.c"))
# 副标题前半句加粗(W6): 发现你的肤色
gen(14, "w6", os.path.join(OUT, "font_zh_14b.c"), extra="发现你的肤色")
print("ALL FONTS DONE")
