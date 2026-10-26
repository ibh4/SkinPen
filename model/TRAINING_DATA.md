# SkinPen V0.5 训练数据与模型管线说明

> 生成日期: 2026-10-13。覆盖 mlpB20/mt5 端侧模型的全部数据来源、清洗规则、标签构造、训练配置与验证结果。

---

## 1. 数据来源

TF 卡上 V0.3 固件（AS7341-V2）的实测采集数据，整包拷贝至:

```
皮肤光谱数据_extracted/皮肤光谱数据/
├── CONFIG/pantone_skintone.csv        # 139 条色卡配置(seq, code, name_en, enabled)
└── DATA/20260101/
    ├── records.csv                    # 汇总: 678 条测量记录(679 行含表头)
    └── 20260101_*.txt                 # 29 个单次明细文件(同一批会话)
```

**采集条件**: AS7341@0x39, I2C0(GPIO41/42), ATIME=29 / ASTEP=599 / GAIN=16X(单帧积分≈50ms), 板载 LED 8mA 亮 30ms, 每次测量连读 10 帧取通过 QC 的帧求均值。

**records.csv 字段**:
`timestamp, session_id, sensor, seq, code, name_en, f1..f8, clear, nir, cv_max, status`

- f1-f8 = 8 通道可见光原始计数(415/445/480/515/555/590/630/680nm)
- clear/NIR = 全通道/NIR 通道(训练时排除，仅作物理边界校验)
- cv_max = 10 帧内最大变异系数(数据稳定性)

## 2. 清洗管线（build_final.py / train_color_model.py 同规则）

| 步骤 | 阈值 | 剩余条数 |
|---|---|---|
| 原始记录 | — | 678 |
| ① cv_max < **0.05** 且 ② 物理边界 f1..f8 总和≥**200** 且 clear≥**80** | CV_THR/SUM_MIN/CLEAR_MIN | 582 |
| ③ 卡内离群: 与该卡中位谱的归一化相对距离 > **0.10** (rdist, ≤2 条的卡全保留) | CARD_THR | **440** |

- 入库色卡: **138 张**(4Y14SP 无有效测量，不入库)；440 条即训练集(mt5 训练即用此口径)
- 清洗明细: `analysis_out/clean_report.csv`、卡统计 `card_stats.csv`、折内明细 `cv_detail.csv`
- 注: V0.3 模板管线(build_final.py, MAD 口径)清洗得 480 条，仅用于 skin_templates.h；mt5 训练管线(train_color_model.py, rdist 口径)为 440 条
- 整理版训练数据(含标签与特征列): `/Users/pwngwc/projects/ESP32/SkinPen-训练数据-440条.csv`(由 tools/build_training_csv.py 生成)

## 3. 标签构造（train_color_model.py）

- **RGB 回归目标**: 138 卡 PANTONE SkinTone 官方色(photoshoplus, AdobeRGB 1998)→sRGB 矩阵转换→归一化 0..1（脚本内 P 表硬编码，与 build_final.py 完全一致）
- **多任务第 4 输出 冷暖**: 由色号第 2 位推导，R 冷粉调=1 / Y 暖金调=0
- **多任务第 5 输出 肤深 dep16**: `(dep-1)*16 + min(level,15) + 8`，范围 0~104（dep=色号首位 1-6，level=色号后两位数字），输出×104 还原

## 4. 特征工程

```
spec8 = sqrt(f1..f8) / sum(sqrt(f1..f8))   # 8 维, 和=1
lnsum = ln(sum(f1..f8))                     # 第 9 维, 保留绝对亮度
X = [spec8, lnsum]                          # "spec8+lnsum", 9 维
```

## 5. 模型选型与训练配置

- 5 折交叉验证（KFold shuffle, random_state=42），每折内 StandardScaler 只在训练折拟合
- 候选: ridge 多项式(rgb/lab)、MLP 16/24-12(relu/tanh)、mlpB 系列(带 lnsum 特征)、多任务 mt5(mlpB16/20/24_12，输出 RGB+冷暖+dep16)
- MLP 统一配置: `MLPRegressor(solver="lbfgs", max_iter=8000, random_state=0)`
- **选中: mlpB20/mt5**（9→20 ReLU→5 线性，305 参数），mt5 系列中 5 折 CV 的 dE 均值最优 → 全量重训导出

## 6. 验证结果（color_model.json 的 cv 字段）

| 指标 | 值 |
|---|---|
| ΔE76 均值 / p95 / max | **1.85 / 3.87 / 6.06** |
| RGB MAE(0-255) | 3.19 |
| 冷暖识别准确率 | **98.9%** |
| 肤深 dep16 MAE | 4.24 |
| V0.4 最近邻基线(留一) ΔE | 0.12 / p95 1.11 / max 4.99 |

> 基线 ΔE 低是因为它评估"卡间可分性"(预测同一张卡的官方色)，而 mt5 是跨记录回归，两者不可直接比较。

**下游验证**:
- 推荐一致性(`validate_v05_recs.py`): V0.5 模型直出 vs V0.4 查表，粉底/口红一致率均 **85.5%**，不一致全部为相邻档位，无业务影响
- 固件对拍(`光谱/skinpen_v2/tools/test_model_host.py`): C 实现 vs Python 逐位一致

## 7. 模型产物链

```
records.csv (678)
  → train_color_model.py → analysis_out/color_model.json
      → 光谱/skinpen_v2/tools/gen_weights_v05.py → main/model_weights.h (float32 精确表示)
      → 固件 main/model.c (spec8+lnsum 特征, double 累加推理)
  → build_final.py → main/skin_templates.h (V0.3 查表模板, 保留用于 JSONL 对比)
```

## 8. 文件清单

| 文件 | 说明 |
|---|---|
| `皮肤光谱数据_extracted/` | 原始采集数据(records.csv + 明细 + 色卡配置) |
| `train_color_model.py` | 训练脚本(清洗→特征→候选 CV→选型→全量重训→导出 JSON) |
| `build_final.py` | V0.3 模板生成脚本(清洗规则同源, 输出 skin_templates.h) |
| `validate_v05_recs.py` | V0.5 vs V0.4 推荐一致性验证 |
| `analysis_out/color_model.json` | 模型权重+标准化参数+138 卡演示光谱(网页/固件同源) |
| `analysis_out/clean_report.csv` | 逐条清洗结果 |
| `analysis_out/card_stats.csv` | 每卡条数/中位谱统计 |
| `analysis_out/cv_detail.csv` | 交叉验证折明细 |
| `analysis_out/templates_final.csv` | 138 卡最终模板 |
| `analysis_out/esp32_templates.h` | 模板 C 头文件(build_final 输出副本) |
| `analysis_out/final_report.txt` | 清洗+验证汇总 |

## 9. 重新训练

```bash
cd /Users/pwngwc/projects/ESP32/皮肤数据
python3 train_color_model.py        # 数据路径在脚本头部 BASE/CSV_PATH
# 换数据后: 同步重跑 build_final.py(V0.3 模板) 与 固件 tools/gen_weights_v05.py(权重)
```

新数据接入方式: 把新固件 TF 的 `/sdcard/data_v2/**/records.txt` 转成 records.csv 同格式(timestamp/seq/code/f1..f8/clear/nir/cv_max)，或直接改脚本读 v2 格式。
