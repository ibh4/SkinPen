// V0.5 颜色回归模型接口 —— mlpB20/mt5 (305 参数, 权重见 model_weights.h)
// 特征 (spec8+lnsum, 与离线管线/网页模拟器一致):
//   spec8[i] = sqrt(f_i) 后归一化, 8 项和 = 1  (只用 f1-f8 可见光通道)
//   feat[8]  = ln( sum(f1..f8) )               (亮度)
// 推理: x=(feat-x_mean)/x_scale -> 隐层20 ReLU -> 输出5 线性
// 输出: [0..2]=R,G,B(0..1) [3]=冷暖得分 [4]=肤深/104
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t  r, g, b;    // 模型 sRGB 输出 0..255 (round+clamp, 显示前经 RGB565 量化)
    uint16_t rgb565;     // RGB565 量化显示色
    bool     cool;       // true=冷粉调(C), false=暖金调(W); 由输出[3]>0.5 判定
    int      dep16;      // 肤深连续值 0..104
} model_out_t;

// mean[10]: AS7341 清洗后均值 {f1..f8, clear, nir}
// 返回 0 成功; 光谱全零返回 -1
int model_predict(const uint16_t mean[10], model_out_t *out);
