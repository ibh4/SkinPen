#pragma once
// 皮肤色号光谱匹配 —— SkinPen V0.3
// 公式: spec = sqrt(f1..f8) 归一化(和=1), 与 138 张色卡模板做欧氏距离最近邻
// 模板: skin_templates.h (离线清洗 480 条有效记录构建, 留一验证 top-1 91.4%)
// 物理边界与离线管线一致: 10通道总和>=200 且 clear>=80

#include <stdint.h>

// 返回最匹配模板索引 [0, SKIN_TPL_COUNT), 无效光谱返回 -1
// dist_out: 可选输出, 最近邻欧氏距离
int skin_match_find(const uint16_t mean[10], float *dist_out);
