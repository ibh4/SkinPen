#pragma once
// 美妆推荐 V0.5 —— 由模型输出(肤色RGB565/冷暖/肤深)驱动, 推荐算法与 V0.4 一致
// 粉底: 冷暖选池(C冷/W暖) -> RGB 最近为主推 + 相邻深浅两档共 3 个
// 口红: 底调过滤 + 肤深区间 -> 明度最接近 (肤色明度-25) 排序的候选列表,
//       区间空或明度差均 >40 时放宽到全底调池; 主推为候选[0]
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char     code[12];    // 色号 "5W"(粉底) / "233"(口红) / "Chili"
    char     name[24];    // 商品中文名(口红: 玫瑰豆沙 等, 粉底为空)
    char     brand[24];   // 品牌/系列 "欧莱雅纷泽" / "MAC" / "欧莱雅 True Match"
    uint16_t rgb565;      // 推荐色 (屏幕色块)
} rec_item_t;

// skin565: 模型输出肤色(RGB565 量化); cool: 冷粉调; dep16: 肤深 0..104
// fnd_out[3]: 浅一档/主推/深一档; best_out: 主推索引 0..2
// lip_out/lip_max: 口红候选输出(主推在前); lip_n_out: 实际数量
// 纯计算无阻塞, 可在任意任务调用
void recommend_v05(uint16_t skin565, bool cool, int dep16,
                   rec_item_t fnd_out[3], int *best_out,
                   rec_item_t lip_out[], int lip_max, int *lip_n_out);
