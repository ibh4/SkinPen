// V0.5 颜色回归模型 (mlpB20/mt5, 305 参数) —— 权重见 model_weights.h (自动生成)
// 特征: spec8[i]=sqrt(f_i)/sum(sqrt(f1..f8)); feat[8]=ln(sum(f1..f8))
// 推理: x=(feat-mean)/scale; h=relu(W1^T x+b1); y=W2^T h+b2
// 数值: 双精度累加, 与 Python/JS(float64) 语义一致; RGB565 量化为统一约定
#include "model.h"

#include "model_weights.h"
#include <math.h>

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

int model_predict(const uint16_t mean[10], model_out_t *out)
{
    // ---- 特征构造 (spec8 + lnsum) ----
    double s8 = 0;
    for (int i = 0; i < 8; i++) s8 += sqrt((double)mean[i]);
    if (s8 <= 0.0) return -1;

    double feat[MODEL_IN_DIM];
    for (int i = 0; i < 8; i++) feat[i] = sqrt((double)mean[i]) / s8;
    double sum_raw = 0;
    for (int i = 0; i < 8; i++) sum_raw += (double)mean[i];
    feat[8] = log(sum_raw);

    // ---- 标准化 ----
    double x[MODEL_IN_DIM];
    for (int i = 0; i < MODEL_IN_DIM; i++)
        x[i] = (feat[i] - MODEL_X_MEAN[i]) / MODEL_X_SCALE[i];

    // ---- 隐层: 9 -> 20, ReLU ----
    double h[MODEL_HID_DIM];
    for (int j = 0; j < MODEL_HID_DIM; j++) {
        double s = MODEL_B1[j];
        for (int i = 0; i < MODEL_IN_DIM; i++) s += x[i] * MODEL_W1[i][j];
        h[j] = s > 0 ? s : 0;
    }

    // ---- 输出层: 20 -> 5, 线性 ----
    double y[MODEL_OUT_DIM];
    for (int j = 0; j < MODEL_OUT_DIM; j++) {
        double s = MODEL_B2[j];
        for (int i = 0; i < MODEL_HID_DIM; i++) s += h[i] * MODEL_W2[i][j];
        y[j] = s;
    }

    // ---- 输出解析: RGB 0..1 -> 0..255, 冷暖, 肤深 ----
    int rgb[3];
    for (int k = 0; k < 3; k++) {
        double v = y[k] < 0 ? 0 : (y[k] > 1 ? 1 : y[k]);
        int q = (int)(v * 255.0 + 0.5);
        if (q < 0) q = 0;
        if (q > 255) q = 255;
        rgb[k] = q;
    }
    out->r = (uint8_t)rgb[0];
    out->g = (uint8_t)rgb[1];
    out->b = (uint8_t)rgb[2];
    out->rgb565 = RGB565(rgb[0], rgb[1], rgb[2]);
    out->cool = y[3] > 0.5;
    int dep = (int)(y[4] * 104.0 + 0.5);
    if (dep < 0) dep = 0;
    if (dep > 104) dep = 104;
    out->dep16 = dep;
    return 0;
}
