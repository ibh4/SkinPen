#pragma once
// LVGL 新版 GUI —— SkinPen V0.5 (横版 320x172, 界面交接包《界面定稿与移植规范》)
// 13 页面: home/prepare/measure/result/foundation/fdetail/lip/why/more/
//          history/tools/cards(工程)/cardlist/failure
// 布局: 所有页无顶部标题栏; 内容区 y=0 h=100; 底栏 y=100 h=52(按钮 48px);
//       屏幕下方 y=152..171 留白。中文多字号字库 font_zh_14/16/18/20/22/24。
// 所有 gui_xxx 更新函数内部加 LVGL 锁, 可在任意任务调用;
// 采集回调携带会话 ID, 迟到的旧会话回调被忽略(取消后不重开结果页)。

#include "esp_err.h"
#include "pantone.h"
#include "recommend.h"
#include <stdbool.h>

#define GUI_LIP_MAX 8      // 口红候选缓存上限(换一支轮换)

// GUI -> 业务层 回调(在 LVGL 任务上下文触发, 回调里只做投递)
typedef struct {
    void (*on_start)(void);        // 开始测色(prepare 页)
    void (*on_cancel)(void);       // 取消当前采集
    void (*on_capture)(void);      // 工程模式采集此卡
    void (*on_prev)(void);         // 工程模式上一张
    void (*on_next)(void);         // 工程模式下一张
    void (*on_select)(int index);  // 色卡列表点选(跳转并进工程页)
    void (*on_save_retry)(void);   // TF 保存重试(more 页)
} gui_cbs_t;

// 色卡状态(工程模式显示)
enum { GUI_CARD_UNTESTED = 0, GUI_CARD_PASS = 1, GUI_CARD_FAIL = 2 };

// 一次测量的完整结果(V0.5 模型直出)
typedef struct {
    int      session;      // 采集会话 ID(与 gui_meas_begin 一致才生效)
    uint8_t  r, g, b;      // 模型 sRGB 输出(0..255, more 页 HEX 显示用)
    uint16_t rgb565;       // RGB565 量化显示色
    bool     cool;         // true=冷粉调(C) / false=暖金调(W)
    int      dep16;        // 肤深 0..104
    float    cv_max;       // 质量指标(通道 CV 最大值)
    uint16_t spec[8];      // f1..f8 通道均值(更多页光谱曲线显示用)
    bool     tf_saved;     // TF 保存成功(失败时 more 页可重试)
    char     tf_file[64];  // TF 记录文件名
    rec_item_t fnd[3];     // 粉底 浅一档/主推/深一档
    int      fnd_best;     // 主推索引 0..2
    rec_item_t lip[GUI_LIP_MAX]; // 口红候选(主推在前, 均通过真实筛选)
    int      lip_n;
} gui_result_t;

// 创建全部页面(需在 LVGL 与显示初始化完成后调用); 启动画面 1.5s 后淡入主页
esp_err_t gui_init(void);

void gui_register_cbs(const gui_cbs_t *cbs);

// ---- 设备自检(异常时在主页显示红字) ----
void gui_set_dev_status(bool sd_ok, int card_count, bool as_ok);

// ---- 工程模式(色卡采集) ----
void gui_fill_cards(int total);                                    // 色卡库规模
void gui_card_mark(int index, int state);                          // 标记已测/失败
void gui_set_card(int index, int total, const card_record_t *card); // 刷新工程页当前卡

// ---- 采集流程(measurement 任务调用; 旧会话回调按 session 过滤) ----
void gui_meas_begin(int session, const card_record_t *card);  // 进采集页
void gui_meas_progress(int session, int i, int total);        // 帧进度
void gui_meas_result(const gui_result_t *res);                // 成功: 结果页/工程页
void gui_meas_failure(int session, const char *reason);       // 质量检查失败: 失败页
void gui_meas_cancel(int session);                            // 取消: 回来源页
void gui_meas_saved(int session, bool ok, const char *fname); // TF 保存状态刷新
