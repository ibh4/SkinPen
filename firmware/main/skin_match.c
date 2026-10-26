// 皮肤色号光谱匹配实现 —— SkinPen V0.3
// 与离线管线(皮肤数据/build_final.py)完全一致的公式:
//   1. 物理边界: sum(10通道)>=200 且 clear>=80 (剔除未贴合/黑物)
//   2. spec = sqrt(f1..f8) / sum(sqrt(f1..f8))  -> 8 维归一化向量
//   3. 与 138 张色卡模板的欧氏距离, 最近者胜
#include "skin_match.h"

#include "skin_templates.h"
#include <math.h>

int skin_match_find(const uint16_t mean[10], float *dist_out)
{
    if (mean == NULL) return -1;

    // ---- 物理边界(与离线清洗一致) ----
    uint32_t sum = 0;
    for (int i = 0; i < 10; i++) sum += mean[i];
    if (sum < 200 || mean[8] < 80) return -1;

    // ---- sqrt(f1..f8) 归一化 ----
    float s[8], tsum = 0;
    for (int i = 0; i < 8; i++) {
        s[i] = sqrtf((float)mean[i]);
        tsum += s[i];
    }
    if (tsum <= 0) return -1;
    for (int i = 0; i < 8; i++) s[i] /= tsum;

    // ---- 最近邻 ----
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < SKIN_TPL_COUNT; k++) {
        float d = 0;
        for (int i = 0; i < 8; i++) {
            float df = s[i] - g_skin_tpl[k].spec[i];
            d += df * df;
        }
        if (d < bd) { bd = d; best = k; }
    }
    if (dist_out) *dist_out = sqrtf(bd);
    return best;
}
