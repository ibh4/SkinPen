// LVGL 新版 GUI —— SkinPen V0.5 (横版 320x172)
// 依据《界面定稿与移植规范》(界面交接包): 所有页无顶部标题栏,
// 内容区 y=0..99 (h=100), 底栏 y=100..151 (h=52, 按钮 48px), 屏底 20px 留白。
// 主题: 背景 #FFF8FC / 正文 #392647 / 主按钮 #AB2865 / 次 #EEE3FF /
//       蓝 #D9EFFF+#174F79 / 粉 #FFE0EA+#8E234C; 肤色与商品色块用真实数据色。
// 页面事件与《skinpen-fragment.html》一致, 返回为层级回退;
// 采集回调按会话 ID 过滤, 取消/重测后迟到的旧回调不会重开结果页。
#include "gui.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

// ---- 主题色 ----
#define COL_BG      lv_color_hex(0xFFF8FC)
#define COL_INK     lv_color_hex(0x392647)
#define COL_MUTED   lv_color_hex(0x756D65)
#define COL_PRIMARY lv_color_hex(0xAB2865)
#define COL_SECOND  lv_color_hex(0xEEE3FF)
#define COL_BLUE    0xD9EFFF
#define COL_BLUE_TX 0x174F79
#define COL_PINK    0xFFE0EA
#define COL_PINK_TX 0x8E234C
#define COL_TITLE   lv_color_hex(0x6640A0)
#define COL_EMPH    lv_color_hex(0xA82661)
#define COL_TRACK   lv_color_hex(0xE8E1D8)
#define COL_FILL    lv_color_hex(0x285B50)
#define COL_OPTBG   lv_color_hex(0xF0E9DF)
#define COL_ROWBG   lv_color_hex(0xF6EBF4)
#define COL_DONE    lv_color_hex(0x176752)
#define COL_WAIT    lv_color_hex(0x7F6575)
#define COL_ERR     lv_color_hex(0xC62828)

// ---- 字体 ----
extern const lv_font_t font_zh_14;
extern const lv_font_t font_zh_14b;
extern const lv_font_t font_zh_16;
extern const lv_font_t font_zh_18;
extern const lv_font_t font_zh_20;
extern const lv_font_t font_zh_22;
extern const lv_font_t font_zh_24;
#define F14  &font_zh_14
#define F14B &font_zh_14b
#define F16  &font_zh_16
#define F18  &font_zh_18
#define F20  &font_zh_20
#define F22  &font_zh_22
#define F24  &font_zh_24

#define MAX_CARDS 160

// ---- 页面枚举 ----
typedef enum {
    PG_HOME = 0, PG_PREPARE, PG_MEASURE, PG_RESULT, PG_FOUNDATION, PG_FDETAIL,
    PG_LIP, PG_WHY, PG_MORE, PG_HISTORY, PG_TOOLS, PG_CARDS, PG_CARDLIST,
    PG_FAILURE, PG_COUNT
} page_t;

static gui_cbs_t s_cbs;
static lv_obj_t *s_scr[PG_COUNT];
static page_t s_page = PG_HOME;

// ---- 会话/流程状态 ----
static int s_session = -1;               // 当前采集会话(迟到回调过滤)
static page_t s_meas_origin = PG_HOME;   // 采集来源(取消/失败回退目标)
static page_t s_fail_origin = PG_HOME;
static bool s_res_valid = false;
static gui_result_t s_res;
static bool s_fav = false;               // 当前测量收藏状态(重测即重置)
static uint8_t s_state[MAX_CARDS];       // 工程模式色卡状态
static int s_cards_total = 0;
static int s_cur_card_index = -1;

// ---- 页面局部状态 ----
static bool s_art_lip = true;            // 主页插画: true 口红 / false 粉底液
static lv_timer_t *s_art_timer = NULL;
static int s_fsel = 1;                   // 粉底候选选择(指向 s_fnd_u 下标)
static int s_lip_idx = 0;                // 口红候选游标(换一支)
static int s_list_start = 0;             // 色卡列表分页起点

// 粉底唯一候选(相邻档到边界时去重) + 标签
static rec_item_t s_fnd_u[3];
static const char *s_fnd_tag[3];
static int s_fnd_n = 3;

// ---- 对象引用 ----
static lv_obj_t *s_art, *s_home_err;
static lv_obj_t *s_mark_lbl, *s_bar;
static lv_obj_t *s_res_swatch, *s_res_title, *s_res_status, *s_res_sum;
static lv_obj_t *s_fopt[3], *s_fchip[3], *s_fcode[3], *s_ftag[3], *s_fview_lbl;
static lv_obj_t *s_fd_swatch, *s_fd_brand, *s_fd_r1, *s_fd_r2, *s_fd_fav_lbl;
static lv_obj_t *s_lip_swatch, *s_lip_pill, *s_lip_name, *s_lip_bc, *s_lip_mut;
static lv_obj_t *s_why_fav_lbl;
static lv_obj_t *s_more_save_lbl, *s_spec_line;
static lv_obj_t *s_spec_dots[8];                 // 8 通道锚点小圆点
static lv_point_t s_spec_pts[64];                // 平滑曲线采样点(Catmull-Rom)
static int s_spec_npts;
static lv_obj_t *s_hist_row, *s_hist_row_lbl, *s_hist_empty_t, *s_hist_empty_s;
static lv_obj_t *s_card_code, *s_card_state;
static lv_obj_t *s_cl_row[2], *s_cl_left[2], *s_cl_right[2];
static lv_obj_t *s_fail_title;

// ---- 前向声明 ----
static void show_page(page_t p);
static void art_timer_cb(lv_timer_t *t);
static void cardlist_refresh(void);
static void foundation_refresh(void);
static void fdetail_refresh(void);
static void lip_refresh(void);
static void more_refresh(void);
static void fill_result_labels(void);

// 页面切换(全部导航统一入口; 数据刷新由各回调/公共接口先行完成)
static void show_page(page_t p)
{
    if (p < 0 || p >= PG_COUNT || s_scr[p] == NULL) return;
    s_page = p;
    lv_scr_load_anim(s_scr[p], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

// ================================================================ 基础控件
// RGB565(16位) -> LVGL 颜色: 展开回 8bit/通道。
// 注意不能直接 lv_color_hex(rgb565) —— 它把入参当 0xRRGGBB(24位),
// 16 位值会被解释成极暗的青绿/深蓝(显示变绿的根因)。
static lv_color_t col565(uint16_t v)
{
    uint8_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
    uint8_t r = (r5 << 3) | (r5 >> 2);
    uint8_t g = (g6 << 2) | (g6 >> 4);
    uint8_t b = (b5 << 3) | (b5 >> 2);
    return lv_color_hex(((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
}

static void lbl_style(lv_obj_t *l, const lv_font_t *f, lv_color_t c)
{
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
}

// 装饰矩形(不可点击, 点击穿透到父对象)
static lv_obj_t *rect(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                      lv_coord_t w, lv_coord_t h, lv_coord_t radius,
                      uint32_t bg, uint32_t border)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, border ? 2 : 0, 0);
    if (border) lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *make_circle(lv_obj_t *parent, lv_coord_t d, lv_coord_t x,
                             lv_coord_t y, uint32_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// 内容区: y=0 h=100 (规范 padding 12/8/8/8 由子元素坐标直接体现)
static lv_obj_t *make_content(lv_obj_t *scr)
{
    lv_obj_t *c = lv_obj_create(scr);
    lv_obj_set_pos(c, 0, 0);
    lv_obj_set_size(c, 320, 100);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

// 底部操作栏: y=100 h=52, 左右4 下4 隙4, 按钮高48 flex 均分
static lv_obj_t *make_footer(lv_obj_t *scr)
{
    lv_obj_t *f = lv_obj_create(scr);
    lv_obj_set_pos(f, 0, 100);
    lv_obj_set_size(f, 320, 52);
    lv_obj_set_style_bg_opa(f, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(f, 0, 0);
    lv_obj_set_style_pad_left(f, 4, 0);
    lv_obj_set_style_pad_right(f, 4, 0);
    lv_obj_set_style_pad_top(f, 0, 0);
    lv_obj_set_style_pad_bottom(f, 4, 0);
    lv_obj_set_style_pad_column(f, 4, 0);
    lv_obj_set_flex_flow(f, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
    return f;
}

static lv_obj_t *btn_common(lv_obj_t *b, const char *txt, lv_color_t bg,
                            lv_color_t tx, const lv_font_t *font,
                            lv_event_cb_t cb, void *ud, lv_obj_t **lbl_out)
{
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t *t = lv_label_create(b);
    lv_label_set_text(t, txt);
    lbl_style(t, font, tx);
    lv_obj_center(t);
    if (lbl_out) *lbl_out = t;
    return b;
}

// 底栏按钮(高48, flex 均分宽, 字18px)
static lv_obj_t *foot_btn(lv_obj_t *foot, const char *txt, uint32_t bg,
                          uint32_t tx, lv_event_cb_t cb, void *ud, lv_obj_t **lbl_out)
{
    lv_obj_t *b = lv_btn_create(foot);
    lv_obj_set_height(b, 48);
    lv_obj_set_flex_grow(b, 1);
    return btn_common(b, txt, lv_color_hex(bg), lv_color_hex(tx), F18, cb, ud, lbl_out);
}

// 固定尺寸按钮
static lv_obj_t *box_btn(lv_obj_t *parent, const char *txt, lv_coord_t x, lv_coord_t y,
                         lv_coord_t w, lv_coord_t h, uint32_t bg, uint32_t tx,
                         const lv_font_t *font, lv_event_cb_t cb, void *ud,
                         lv_obj_t **lbl_out)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    return btn_common(b, txt, lv_color_hex(bg), lv_color_hex(tx), font, cb, ud, lbl_out);
}

static void btn_text(lv_obj_t *lbl, const char *txt)
{
    if (lbl) lv_label_set_text(lbl, txt);
}

// ================================================================ 结果/刷新
// 粉底唯一候选: 相邻索引到边界时与主推重复, 去重为 2-3 个
static void fndlist_build(const gui_result_t *r)
{
    int lo = r->fnd_best > 0 ? r->fnd_best - 1 : 0;
    int hi = r->fnd_best < 8 ? r->fnd_best + 1 : 8;
    int poolidx[3] = { lo, r->fnd_best, hi };
    s_fnd_n = 0;
    int last = -1;
    for (int i = 0; i < 3; i++) {
        if (poolidx[i] == last) continue;            // 边界去重
        s_fnd_u[s_fnd_n] = r->fnd[i];
        s_fnd_tag[s_fnd_n] = (poolidx[i] < r->fnd_best) ? "浅一档"
                           : ((poolidx[i] > r->fnd_best) ? "深一档" : "主推");
        if (poolidx[i] == r->fnd_best) s_fsel = s_fnd_n;   // 默认选中主推
        last = poolidx[i];
        s_fnd_n++;
    }
}

static void fill_result_labels(void)
{
    char buf[48];
    lv_obj_set_style_bg_color(s_res_swatch, col565(s_res.rgb565), 0);
    lv_label_set_text(s_res_title, s_res.cool ? "偏冷粉调" : "偏暖金调");
    if (s_res.tf_saved) {
        lv_label_set_text(s_res_status, "采集稳定");
        lv_obj_set_style_text_color(s_res_status, COL_INK, 0);
    } else {
        lv_label_set_text(s_res_status, "TF 保存失败");
        lv_obj_set_style_text_color(s_res_status, COL_ERR, 0);
    }
    snprintf(buf, sizeof(buf), "粉底 %s · 唇色 %s",
             s_res.fnd[1].code, s_res.lip[0].code);
    lv_label_set_text(s_res_sum, buf);

    // 历史页行(有本次结果才显示)
    if (s_res_valid) {
        snprintf(buf, sizeof(buf), "本次 · 未指定 · %s >",
                 s_res.cool ? "偏冷粉调" : "偏暖金调");
        lv_label_set_text(s_hist_row_lbl, buf);
        lv_obj_clear_flag(s_hist_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hist_empty_t, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_hist_empty_s, LV_OBJ_FLAG_HIDDEN);
    }
}

static void foundation_refresh(void)
{
    for (int i = 0; i < s_fnd_n; i++) {
        lv_obj_clear_flag(s_fopt[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_fchip[i], col565(s_fnd_u[i].rgb565), 0);
        lv_label_set_text(s_fcode[i], s_fnd_u[i].code);
        lv_label_set_text(s_ftag[i], s_fnd_tag[i]);
        lv_obj_set_style_border_width(s_fopt[i], i == s_fsel ? 2 : 0, 0);
        if (i == s_fsel) lv_obj_set_style_border_color(s_fopt[i], COL_FILL, 0);
    }
    for (int i = s_fnd_n; i < 3; i++)
        lv_obj_add_flag(s_fopt[i], LV_OBJ_FLAG_HIDDEN);
    char buf[24];
    snprintf(buf, sizeof(buf), "查看 %s", s_fnd_u[s_fsel].code);
    btn_text(s_fview_lbl, buf);
}

static void fdetail_refresh(void)
{
    char buf[48];
    lv_obj_set_style_bg_color(s_fd_swatch, col565(s_fnd_u[s_fsel].rgb565), 0);
    snprintf(buf, sizeof(buf), "%s %s", s_fnd_u[s_fsel].brand, s_fnd_u[s_fsel].code);
    lv_label_set_text(s_fd_brand, buf);
    bool is_best = strcmp(s_fnd_tag[s_fsel], "主推") == 0;
    lv_label_set_text(s_fd_r1, is_best ? (s_res.cool ? "冷调池中颜色最接近"
                                                     : "暖调池中颜色最接近")
                                       : "主推相邻档，供实物试色");
    lv_label_set_text(s_fd_r2, "实际效果请与下颌、颈部对照");
    btn_text(s_fd_fav_lbl, s_fav ? "已收藏" : "收藏搭配");
}

static void lip_refresh(void)
{
    const rec_item_t *it = &s_res.lip[s_lip_idx];
    lv_obj_set_style_bg_color(s_lip_swatch, col565(s_res.rgb565), 0);
    lv_obj_set_style_bg_color(s_lip_pill, col565(it->rgb565), 0);
    lv_label_set_text(s_lip_name, it->name[0] ? it->name : it->code);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s · %s", it->brand, it->code);
    lv_label_set_text(s_lip_bc, buf);
    lv_label_set_text(s_lip_mut, "柔和配色 · 非上唇模拟");
    btn_text(s_why_fav_lbl, s_fav ? "已收藏" : "收藏搭配");
}

// 光谱曲线几何常量(更多页内容区内)
#define SPEC_X0     18          // 曲线左缘
#define SPEC_X1     302         // 曲线右缘
#define SPEC_TOP    26          // 曲线顶部(数值最大处)
#define SPEC_BOT    76          // 曲线底部(数值最小处)

static void more_refresh(void)
{
    btn_text(s_more_save_lbl, s_res.tf_saved ? "已保存" : "保存");

    // ---- f1..f8 光谱平滑曲线(Catmull-Rom 样条, 红色) ----
    uint16_t v[8];
    memcpy(v, s_res.spec, sizeof(v));
    uint16_t vmin = v[0], vmax = v[0];
    for (int i = 1; i < 8; i++) {
        if (v[i] < vmin) vmin = v[i];
        if (v[i] > vmax) vmax = v[i];
    }
    // 上下留 15% 余量; 变化过小时放大到最小跨度, 避免曲线压成直线/噪声放大
    int span = vmax - vmin;
    if (span < 120) span = 120;
    int lo = (int)vmin - span / 7;
    int hi = (int)vmax + span / 7;

    int ax[8];                            // 8 个锚点坐标
    int ay[8];
    for (int i = 0; i < 8; i++) {
        ax[i] = SPEC_X0 + i * (SPEC_X1 - SPEC_X0) / 7;
        ay[i] = SPEC_BOT - (int)v[i] * (SPEC_BOT - SPEC_TOP) / (hi - lo);
        if (ay[i] < SPEC_TOP) ay[i] = SPEC_TOP;
        if (ay[i] > SPEC_BOT) ay[i] = SPEC_BOT;
    }

    // Catmull-Rom: 每段采 8 个点, 边界用端点补齐
    s_spec_npts = 0;
    for (int i = 0; i < 7; i++) {
        int p0x = ax[i > 0 ? i - 1 : 0],     p0y = ay[i > 0 ? i - 1 : 0];
        int p1x = ax[i],                     p1y = ay[i];
        int p2x = ax[i + 1],                 p2y = ay[i + 1];
        int p3x = ax[i < 6 ? i + 2 : 7],     p3y = ay[i < 6 ? i + 2 : 7];
        int steps = (i < 6) ? 8 : 9;         // 最后一段含终点
        for (int s = 0; s < steps && s_spec_npts < 64; s++) {
            float t = (float)s / 8.0f;
            float t2 = t * t, t3 = t2 * t;
            // 标准 Catmull-Rom 系数(均匀节点)
            float w0 = -0.5f * t3 + t2 - 0.5f * t;
            float w1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
            float w2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
            float w3 = 0.5f * t3 - 0.5f * t2;
            s_spec_pts[s_spec_npts].x = (lv_coord_t)(w0 * p0x + w1 * p1x + w2 * p2x + w3 * p3x);
            s_spec_pts[s_spec_npts].y = (lv_coord_t)(w0 * p0y + w1 * p1y + w2 * p2y + w3 * p3y);
            s_spec_npts++;
        }
    }
    if (s_spec_line) lv_line_set_points(s_spec_line, s_spec_pts, s_spec_npts);
    for (int i = 0; i < 8; i++)
        if (s_spec_dots[i]) lv_obj_set_pos(s_spec_dots[i], ax[i] - 3, ay[i] - 3);
}

static void cards_state_refresh(int index)
{
    if (index < 0 || index >= s_cards_total || s_card_state == NULL) return;
    if (s_state[index] == GUI_CARD_PASS) {
        lv_label_set_text(s_card_state, "已采集");
        lv_obj_set_style_text_color(s_card_state, COL_DONE, 0);
    } else if (s_state[index] == GUI_CARD_FAIL) {
        lv_label_set_text(s_card_state, "采集失败");
        lv_obj_set_style_text_color(s_card_state, COL_ERR, 0);
    } else {
        lv_label_set_text(s_card_state, "未采集");
        lv_obj_set_style_text_color(s_card_state, COL_WAIT, 0);
    }
}

static void cardlist_refresh(void)
{
    for (int r = 0; r < 2; r++) {
        int i = s_list_start + r;
        if (i >= s_cards_total) {
            lv_obj_add_flag(s_cl_row[r], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_cl_row[r], LV_OBJ_FLAG_HIDDEN);
        const card_record_t *c = pantone_get(i);
        char buf[24];
        if (c) snprintf(buf, sizeof(buf), "%03d %s", c->seq, c->code);
        else   snprintf(buf, sizeof(buf), "%03d", i + 1);
        lv_label_set_text(s_cl_left[r], buf);
        if (s_state[i] == GUI_CARD_PASS) {
            lv_label_set_text(s_cl_right[r], "已采集");
            lv_obj_set_style_text_color(s_cl_right[r], COL_DONE, 0);
        } else if (s_state[i] == GUI_CARD_FAIL) {
            lv_label_set_text(s_cl_right[r], "采集失败");
            lv_obj_set_style_text_color(s_cl_right[r], COL_ERR, 0);
        } else {
            lv_label_set_text(s_cl_right[r], "选择 >");
            lv_obj_set_style_text_color(s_cl_right[r], COL_WAIT, 0);
        }
    }
}

// ================================================================ 主页插画
static void art_build(void)
{
    lv_obj_clean(s_art);
    lv_obj_set_style_bg_color(s_art, lv_color_hex(s_art_lip ? 0xFFDAE8 : 0xDCEFFF), 0);
    if (s_art_lip) {                                  // 口红(62px 稿 x1.2258)
        rect(s_art, 24, 42, 28, 32, 4, 0x653C83, 0);  // 底座
        rect(s_art, 24, 37, 28, 7, 2, 0xEEBF67, 0);   // 金环
        rect(s_art, 28, 9, 21, 32, 10, 0xE63E79, 0);  // 膏体(顶部圆角)
    } else {                                          // 粉底液
        rect(s_art, 21, 29, 37, 40, 6, 0xDBAB78, 0xB77845);  // 瓶身
        rect(s_art, 26, 42, 27, 15, 2, 0xFFF4DA, 0);  // 标签
        rect(s_art, 28, 11, 22, 21, 4, 0x70539D, 0);  // 盖
    }
}

static void art_timer_cb(lv_timer_t *t)
{
    if (s_page != PG_HOME) { lv_timer_del(t); s_art_timer = NULL; return; }
    s_art_lip = !s_art_lip;
    art_build();
}

static void cb_art_click(lv_event_t *e)
{
    (void)e;
    s_art_lip = !s_art_lip;
    art_build();
}

// ================================================================ 事件回调
static void cb_goto(lv_event_t *e)          // 通用: 跳转到 user_data 页面
{
    show_page((page_t)(intptr_t)lv_event_get_user_data(e));
}

static void cb_prepare_start(lv_event_t *e)
{
    (void)e;
    s_meas_origin = PG_HOME;
    if (s_cbs.on_start) s_cbs.on_start();
}

static void cb_measure_cancel(lv_event_t *e)
{
    (void)e;
    if (s_cbs.on_cancel) s_cbs.on_cancel();
}

static void cb_cards_capture(lv_event_t *e)
{
    (void)e;
    s_meas_origin = PG_CARDS;
    if (s_cbs.on_capture) s_cbs.on_capture();
}

static void cb_cards_prev(lv_event_t *e) { (void)e; if (s_cbs.on_prev) s_cbs.on_prev(); }
static void cb_cards_next(lv_event_t *e) { (void)e; if (s_cbs.on_next) s_cbs.on_next(); }

static void cb_cl_row(lv_event_t *e)
{
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    int idx = s_list_start + r;
    if (idx >= s_cards_total) return;
    if (s_cbs.on_select) s_cbs.on_select(idx);
    show_page(PG_CARDS);                 // 卡数据由 gui_set_card 到达后刷新
}

static void cb_cl_page(lv_event_t *e)
{
    bool next = (bool)(intptr_t)lv_event_get_user_data(e);
    if (s_cards_total <= 0) return;
    int step = next ? 2 : -2;
    s_list_start = (s_list_start + step + s_cards_total) % s_cards_total;
    s_list_start -= s_list_start & 1;    // 对齐偶数页
    cardlist_refresh();
}

static void cb_fopt_click(lv_event_t *e)
{
    s_fsel = (int)(intptr_t)lv_event_get_user_data(e);
    foundation_refresh();
}

static void cb_fnd_view(lv_event_t *e) { (void)e; fdetail_refresh(); show_page(PG_FDETAIL); }
static void cb_fnd_open(lv_event_t *e) { (void)e; foundation_refresh(); show_page(PG_FOUNDATION); }
static void cb_lip_open(lv_event_t *e) { (void)e; lip_refresh(); show_page(PG_LIP); }
static void cb_more_open(lv_event_t *e) { (void)e; more_refresh(); show_page(PG_MORE); }

static void cb_lip_next(lv_event_t *e)
{
    (void)e;
    if (s_res.lip_n > 1) s_lip_idx = (s_lip_idx + 1) % s_res.lip_n;
    lip_refresh();
}

static void cb_fav_toggle(lv_event_t *e)
{
    (void)e;
    s_fav = !s_fav;
    const char *txt = s_fav ? "已收藏" : "收藏搭配";
    btn_text(s_fd_fav_lbl, txt);
    btn_text(s_why_fav_lbl, txt);
}

static void cb_more_save(lv_event_t *e)
{
    (void)e;
    if (!s_res.tf_saved && s_cbs.on_save_retry) s_cbs.on_save_retry();
}

static void cb_hist_row(lv_event_t *e) { (void)e; fill_result_labels(); show_page(PG_RESULT); }

static void cb_tools_failure(lv_event_t *e)   // 工具页异常说明入口(信息页, 不执行采集)
{
    (void)e;
    s_fail_origin = PG_TOOLS;
    lv_label_set_text(s_fail_title, "本次采集未通过");
    show_page(PG_FAILURE);
}

static void cb_fail_back(lv_event_t *e) { (void)e; show_page(s_fail_origin); }
static void cb_fail_retest(lv_event_t *e) { (void)e; show_page(PG_PREPARE); }

// ================================================================ 页面构建
static void build_home(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_HOME] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);

    // 左侧 76x76 圆形插画(口红/粉底液 3s 轮播 + 点击切换)
    s_art = make_circle(c, 76, 8, 12, 0xFFDAE8);
    lv_obj_add_event_cb(s_art, cb_art_click, LV_EVENT_CLICKED, NULL);
    art_build();

    // 右侧文字区 x=90 宽222
    lv_obj_t *t1 = lv_label_create(c);
    lv_label_set_text(t1, "端侧模型光谱测肤色");
    lbl_style(t1, F20, COL_TITLE);
    lv_obj_align(t1, LV_ALIGN_TOP_LEFT, 90, 24);

    lv_obj_t *t2 = lv_label_create(c);                // 副标题前半句(加粗)
    lv_label_set_text(t2, "发现你的肤色");
    lbl_style(t2, F14B, COL_EMPH);
    lv_obj_align(t2, LV_ALIGN_TOP_LEFT, 90, 54);

    lv_obj_t *t3 = lv_label_create(c);                // 副标题后半句
    lv_label_set_text(t3, " · 粉底与口红搭配");
    lbl_style(t3, F14, COL_INK);
    lv_obj_align_to(t3, t2, LV_ALIGN_OUT_RIGHT_TOP, 2, 0);

    // 设备自检异常红字(平时空)
    s_home_err = lv_label_create(c);
    lv_label_set_text(s_home_err, "");
    lbl_style(s_home_err, F14, COL_ERR);
    lv_obj_align(s_home_err, LV_ALIGN_TOP_LEFT, 90, 76);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "记录", COL_BLUE, COL_BLUE_TX, cb_goto, (void *)(intptr_t)PG_HISTORY, NULL);
    foot_btn(f, "工具", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_TOOLS, NULL);
    foot_btn(f, "开始测色", 0xAB2865, 0xFFFFFF, cb_goto, (void *)(intptr_t)PG_PREPARE, NULL);
}

static void build_prepare(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_PREPARE] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);
    lv_obj_t *t = lv_label_create(c);
    lv_label_set_text(t, "对准洁净、干燥的皮肤");
    lbl_style(t, F24, COL_INK);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_HOME, NULL);
    foot_btn(f, "开始采集", 0xAB2865, 0xFFFFFF, cb_prepare_start, NULL, NULL);
}

static void build_measure(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_MEASURE] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);

    // 左: 计数圆圈 65x65
    lv_obj_t *mark = make_circle(c, 65, 8, 7, 0xFFFFFF);
    lv_obj_set_style_bg_opa(mark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mark, 3, 0);
    lv_obj_set_style_border_color(mark, COL_FILL, 0);
    s_mark_lbl = lv_label_create(mark);
    lv_label_set_text(s_mark_lbl, "0/10");
    lbl_style(s_mark_lbl, F24, COL_FILL);
    lv_obj_center(s_mark_lbl);

    // 右: 提示 + 进度条
    lv_obj_t *t1 = lv_label_create(c);
    lv_label_set_text(t1, "保持不动");
    lbl_style(t1, F22, COL_INK);
    lv_obj_align(t1, LV_ALIGN_TOP_LEFT, 81, 0);

    lv_obj_t *t2 = lv_label_create(c);
    lv_label_set_text(t2, "正在收集颜色信息");
    lbl_style(t2, F14, COL_MUTED);
    lv_obj_align(t2, LV_ALIGN_TOP_LEFT, 81, 36);

    s_bar = lv_bar_create(c);
    lv_obj_set_pos(s_bar, 81, 60);
    lv_obj_set_size(s_bar, 223, 8);
    lv_bar_set_range(s_bar, 0, 10);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar, COL_FILL, LV_PART_INDICATOR);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "取消", 0xEEE3FF, 0x392647, cb_measure_cancel, NULL, NULL);
}

static void build_result(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_RESULT] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);
    s_res_swatch = make_circle(c, 76, 8, 2, 0xCCCCCC);

    s_res_title = lv_label_create(c);
    lv_label_set_text(s_res_title, "------");
    lbl_style(s_res_title, F22, COL_INK);
    lv_obj_align(s_res_title, LV_ALIGN_TOP_LEFT, 92, 6);

    s_res_status = lv_label_create(c);
    lv_label_set_text(s_res_status, "");
    lbl_style(s_res_status, F16, COL_INK);
    lv_obj_align(s_res_status, LV_ALIGN_TOP_LEFT, 92, 40);

    s_res_sum = lv_label_create(c);
    lv_label_set_text(s_res_sum, "");
    lbl_style(s_res_sum, F14, COL_MUTED);
    lv_obj_align(s_res_sum, LV_ALIGN_TOP_LEFT, 92, 64);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "完成", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_HOME, NULL);
    foot_btn(f, "粉底", COL_BLUE, COL_BLUE_TX, cb_fnd_open, NULL, NULL);
    foot_btn(f, "口红", COL_PINK, COL_PINK_TX, cb_lip_open, NULL, NULL);
    foot_btn(f, "更多", 0xEEE3FF, 0x392647, cb_more_open, NULL, NULL);
}

static void build_foundation(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_FOUNDATION] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);

    // 三个候选按钮 (h=78, 字16px), 相邻档到边界时隐藏多余按钮
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_btn_create(c);
        lv_obj_set_size(b, 97, 78);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, i * 104, 1);
        lv_obj_set_style_bg_color(b, COL_OPTBG, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(b, cb_fopt_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_fopt[i] = b;

        s_fchip[i] = rect(b, 4, 3, 89, 30, 4, 0xDDDDDD, 0);
        s_fcode[i] = lv_label_create(b);
        lv_label_set_text(s_fcode[i], "----");
        lbl_style(s_fcode[i], F16, COL_INK);
        lv_obj_align(s_fcode[i], LV_ALIGN_TOP_MID, 0, 36);
        s_ftag[i] = lv_label_create(b);
        lv_label_set_text(s_ftag[i], "");
        lbl_style(s_ftag[i], F14, COL_MUTED);
        lv_obj_align(s_ftag[i], LV_ALIGN_TOP_MID, 0, 57);
    }

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_RESULT, NULL);
    foot_btn(f, "查看", 0xAB2865, 0xFFFFFF, cb_fnd_view, NULL, &s_fview_lbl);
}

static void build_fdetail(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_FDETAIL] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);
    s_fd_swatch = make_circle(c, 76, 8, 2, 0xCCCCCC);

    s_fd_brand = lv_label_create(c);
    lv_label_set_text(s_fd_brand, "");
    lbl_style(s_fd_brand, F16, COL_INK);
    lv_obj_align(s_fd_brand, LV_ALIGN_TOP_LEFT, 92, 8);

    s_fd_r1 = lv_label_create(c);
    lv_label_set_text(s_fd_r1, "");
    lbl_style(s_fd_r1, F14, COL_MUTED);
    lv_obj_align(s_fd_r1, LV_ALIGN_TOP_LEFT, 92, 36);

    s_fd_r2 = lv_label_create(c);
    lv_label_set_text(s_fd_r2, "");
    lbl_style(s_fd_r2, F14, COL_MUTED);
    lv_obj_align(s_fd_r2, LV_ALIGN_TOP_LEFT, 92, 56);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_FOUNDATION, NULL);
    foot_btn(f, "收藏搭配", 0xAB2865, 0xFFFFFF, cb_fav_toggle, NULL, &s_fd_fav_lbl);
}

static void build_lip(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_LIP] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);
    s_lip_swatch = make_circle(c, 76, 8, 2, 0xCCCCCC);
    s_lip_pill = rect(s_lip_swatch, 12, 26, 52, 24, 12, 0xDDDDDD, 0);  // 唇色胶囊

    s_lip_name = lv_label_create(c);
    lv_label_set_text(s_lip_name, "");
    lbl_style(s_lip_name, F22, COL_INK);
    lv_obj_align(s_lip_name, LV_ALIGN_TOP_LEFT, 92, 2);

    s_lip_bc = lv_label_create(c);
    lv_label_set_text(s_lip_bc, "");
    lbl_style(s_lip_bc, F16, COL_INK);
    lv_obj_align(s_lip_bc, LV_ALIGN_TOP_LEFT, 92, 36);

    s_lip_mut = lv_label_create(c);
    lv_label_set_text(s_lip_mut, "");
    lbl_style(s_lip_mut, F14, COL_MUTED);
    lv_obj_align(s_lip_mut, LV_ALIGN_TOP_LEFT, 92, 60);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_RESULT, NULL);
    foot_btn(f, "换一支", 0xEEE3FF, 0x392647, cb_lip_next, NULL, NULL);
    foot_btn(f, "为什么", 0xAB2865, 0xFFFFFF, cb_goto, (void *)(intptr_t)PG_WHY, NULL);
}

static void build_why(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_WHY] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);
    lv_obj_t *l1 = lv_label_create(c);
    lv_label_set_text(l1, "按底调、肤深范围筛选");
    lbl_style(l1, F16, COL_INK);
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 8, 8);

    lv_obj_t *l2 = lv_label_create(c);
    lv_label_set_text(l2, "再比较与肤色的明暗关系");
    lbl_style(l2, F16, COL_INK);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 8, 34);

    lv_obj_t *l3 = lv_label_create(c);
    lv_label_set_text(l3, "口红明度贴近肤色明度-25");
    lbl_style(l3, F14, COL_MUTED);
    lv_obj_align(l3, LV_ALIGN_TOP_LEFT, 8, 60);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_LIP, NULL);
    foot_btn(f, "收藏搭配", 0xAB2865, 0xFFFFFF, cb_fav_toggle, NULL, &s_why_fav_lbl);
}

// more 页: 光谱曲线面板(替换原 部位/显示色值 信息行)
static void build_more(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_MORE] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);

    // 面板: 白底圆角 + 细边框
    lv_obj_t *panel = rect(c, 8, 2, 304, 96, 8, 0xFFFFFF, 0xE4DDD5);

    // 标题小字
    lv_obj_t *cap = lv_label_create(panel);
    lv_label_set_text(cap, "光谱 f1-f8");
    lbl_style(cap, F14, COL_MUTED);
    lv_obj_set_pos(cap, 10, 6);

    // 平滑曲线(红色 2px)
    s_spec_line = lv_line_create(panel);
    lv_line_set_points(s_spec_line, s_spec_pts, 2);
    lv_obj_set_style_line_color(s_spec_line, lv_color_hex(0xE02020), 0);
    lv_obj_set_style_line_width(s_spec_line, 2, 0);
    lv_obj_set_style_line_rounded(s_spec_line, true, 0);
    lv_obj_clear_flag(s_spec_line, LV_OBJ_FLAG_CLICKABLE);

    // 8 通道锚点小圆点(红)
    for (int i = 0; i < 8; i++) {
        lv_obj_t *d = lv_obj_create(panel);
        lv_obj_set_size(d, 6, 6);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(0xE02020), 0);
        lv_obj_set_style_border_width(d, 1, 0);
        lv_obj_set_style_border_color(d, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(d, -20, -20);              // 初始移出面板, 刷新后定位
        s_spec_dots[i] = d;
    }

    // 通道刻度 F1..F8
    for (int i = 0; i < 8; i++) {
        lv_obj_t *t = lv_label_create(panel);
        char nm[8];
        snprintf(nm, sizeof(nm), "F%d", i + 1);
        lv_label_set_text(t, nm);
        lbl_style(t, F14, COL_MUTED);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(t, SPEC_X0 + i * (SPEC_X1 - SPEC_X0) / 7 - 7, 80);
    }

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_RESULT, NULL);
    foot_btn(f, "保存", 0xAB2865, 0xFFFFFF, cb_more_save, NULL, &s_more_save_lbl);
    foot_btn(f, "重测", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_PREPARE, NULL);
}

static void build_history(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_HISTORY] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);

    s_hist_row = box_btn(c, "本次", 8, 16, 304, 48, 0xF6EBF4, 0x392647,
                         F18, cb_hist_row, NULL, &s_hist_row_lbl);
    lv_obj_add_flag(s_hist_row, LV_OBJ_FLAG_HIDDEN); // 无记录时隐藏

    s_hist_empty_t = lv_label_create(c);
    lv_label_set_text(s_hist_empty_t, "还没有记录");
    lbl_style(s_hist_empty_t, F22, COL_INK);
    lv_obj_align(s_hist_empty_t, LV_ALIGN_TOP_LEFT, 8, 12);

    s_hist_empty_s = lv_label_create(c);
    lv_label_set_text(s_hist_empty_s, "测量后可保存肤色与搭配");
    lbl_style(s_hist_empty_s, F16, COL_MUTED);
    lv_obj_align(s_hist_empty_s, LV_ALIGN_TOP_LEFT, 8, 46);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_HOME, NULL);
    foot_btn(f, "开始测色", 0xAB2865, 0xFFFFFF, cb_goto, (void *)(intptr_t)PG_PREPARE, NULL);
}

static void build_tools(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_TOOLS] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);
    box_btn(c, "色卡采集 >", 8, 0, 149, 80, 0xEEE3FF, 0x392647, F18,
            cb_goto, (void *)(intptr_t)PG_CARDS, NULL);
    box_btn(c, "查看异常反馈 >", 165, 0, 149, 80, 0xEEE3FF, 0x392647, F18,
            cb_tools_failure, NULL, NULL);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_HOME, NULL);
}

static void build_cards(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_CARDS] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);
    s_card_code = lv_label_create(c);
    lv_label_set_text(s_card_code, "------");
    lbl_style(s_card_code, F24, COL_INK);
    lv_obj_align(s_card_code, LV_ALIGN_TOP_LEFT, 8, 0);

    s_card_state = lv_label_create(c);
    lv_label_set_text(s_card_state, "未采集");
    lbl_style(s_card_state, F14, COL_WAIT);
    lv_obj_align(s_card_state, LV_ALIGN_TOP_RIGHT, 0, 10);

    box_btn(c, "上一张", 8, 32, 98, 48, 0xEEE3FF, 0x392647, F18, cb_cards_prev, NULL, NULL);
    box_btn(c, "色卡列表", 110, 32, 98, 48, 0xEEE3FF, 0x392647, F18,
            cb_goto, (void *)(intptr_t)PG_CARDLIST, NULL);
    box_btn(c, "下一张", 212, 32, 98, 48, 0xEEE3FF, 0x392647, F18, cb_cards_next, NULL, NULL);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回工具", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_TOOLS, NULL);
    foot_btn(f, "采集此卡", 0xAB2865, 0xFFFFFF, cb_cards_capture, NULL, NULL);
}

static void build_cardlist(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_CARDLIST] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);
    for (int r = 0; r < 2; r++) {
        s_cl_row[r] = box_btn(c, "", 8, 4 + r * 49, 304, 47, 0xF6EBF4, 0x392647,
                              F18, cb_cl_row, (void *)(intptr_t)r, NULL);
        lv_obj_t *right = lv_label_create(s_cl_row[r]);
        lv_label_set_text(right, "");
        lbl_style(right, F14, COL_WAIT);
        lv_obj_align(right, LV_ALIGN_RIGHT_MID, -12, 0);
        s_cl_right[r] = right;
        s_cl_left[r] = lv_obj_get_child(s_cl_row[r], 0);   // btn_common 创建的文本
    }

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_goto, (void *)(intptr_t)PG_CARDS, NULL);
    foot_btn(f, "上一页", 0xEEE3FF, 0x392647, cb_cl_page, (void *)(intptr_t)false, NULL);
    foot_btn(f, "下一页", 0xEEE3FF, 0x392647, cb_cl_page, (void *)(intptr_t)true, NULL);
}

static void build_failure(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    s_scr[PG_FAILURE] = s;
    lv_obj_set_style_bg_color(s, COL_BG, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *c = make_content(s);
    s_fail_title = lv_label_create(c);
    lv_label_set_text(s_fail_title, "本次采集未通过");
    lbl_style(s_fail_title, F22, COL_INK);
    lv_obj_align(s_fail_title, LV_ALIGN_TOP_LEFT, 8, 4);

    lv_obj_t *l2 = lv_label_create(c);
    lv_label_set_text(l2, "保持探头位置后重试");
    lbl_style(l2, F16, COL_INK);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 8, 38);

    lv_obj_t *l3 = lv_label_create(c);
    lv_label_set_text(l3, "不生成新的肤色与推荐");
    lbl_style(l3, F14, COL_MUTED);
    lv_obj_align(l3, LV_ALIGN_TOP_LEFT, 8, 62);

    lv_obj_t *f = make_footer(s);
    foot_btn(f, "返回", 0xEEE3FF, 0x392647, cb_fail_back, NULL, NULL);
    foot_btn(f, "重新测量", 0xAB2865, 0xFFFFFF, cb_fail_retest, NULL, NULL);
}

// ---------------------------------------------------------------- 启动画面
// 白底三行: SkinPen / 皮肤色号检测笔 / 粉底口红推荐(逐字彩色)
static lv_obj_t *scr_splash;

static void splash_timer_cb(lv_timer_t *t)
{
    lv_timer_del(t);
    lv_scr_load_anim(s_scr[PG_HOME], LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, true);
}

static void build_splash(void)
{
    scr_splash = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_splash, COL_BG, 0);
    lv_obj_clear_flag(scr_splash, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l1 = lv_label_create(scr_splash);
    lv_label_set_recolor(l1, true);
    // recolor 语法: "#RRGGBB 文字#" —— 色值后必须有空格
    lv_label_set_text(l1, "#ff2d78 Skin##00b0ff Pen#");
    lbl_style(l1, &lv_font_montserrat_20, COL_INK);
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t *l2 = lv_label_create(scr_splash);
    lv_label_set_text(l2, "皮肤色号检测笔");
    lbl_style(l2, F16, COL_MUTED);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 80);

    lv_obj_t *l3 = lv_label_create(scr_splash);
    lv_label_set_recolor(l3, true);
    lv_label_set_text(l3,
        "#e91e63 粉##f4511e 底##ffa000 口##d32f2f 红##388e3c 推##3b6fb5 荐#");
    lbl_style(l3, F16, COL_INK);
    lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, 108);
}

// ================================================================ 对外接口
esp_err_t gui_init(void)
{
    if (lvgl_port_lock(200) != true) return ESP_ERR_INVALID_STATE;

    build_splash();
    build_home();
    build_prepare();
    build_measure();
    build_result();
    build_foundation();
    build_fdetail();
    build_lip();
    build_why();
    build_more();
    build_history();
    build_tools();
    build_cards();
    build_cardlist();
    build_failure();

    lv_scr_load(scr_splash);
    lv_timer_create(splash_timer_cb, 1500, NULL);     // 1.5s 后淡入主页
    s_art_timer = lv_timer_create(art_timer_cb, 3000, NULL);  // 插画轮播

    lvgl_port_unlock();
    return ESP_OK;
}

void gui_register_cbs(const gui_cbs_t *cbs)
{
    if (cbs) s_cbs = *cbs;
}

void gui_set_dev_status(bool sd_ok, int card_count, bool as_ok)
{
    if (lvgl_port_lock(200) != true) return;
    char buf[48] = { 0 };
    if (!sd_ok)       strcat(buf, "TF异常 ");
    if (card_count == 0) strcat(buf, "色卡异常 ");
    if (!as_ok)       strcat(buf, "传感器异常");
    lv_label_set_text(s_home_err, buf);
    lvgl_port_unlock();
}

void gui_fill_cards(int total)
{
    if (lvgl_port_lock(200) != true) return;
    s_cards_total = (total > MAX_CARDS) ? MAX_CARDS : total;
    memset(s_state, 0, sizeof(s_state));
    s_list_start = 0;
    cardlist_refresh();
    lvgl_port_unlock();
}

void gui_card_mark(int index, int state)
{
    if (index < 0 || index >= MAX_CARDS) return;
    if (lvgl_port_lock(200) != true) return;
    s_state[index] = (uint8_t)state;
    if (index == s_cur_card_index) cards_state_refresh(index);
    if (s_page == PG_CARDLIST) cardlist_refresh();
    lvgl_port_unlock();
}

void gui_set_card(int index, int total, const card_record_t *card)
{
    if (lvgl_port_lock(200) != true) return;
    s_cards_total = (total > MAX_CARDS) ? MAX_CARDS : total;
    s_cur_card_index = index;
    if (card) {
        lv_label_set_text(s_card_code, card->code);
        cards_state_refresh(index);
    }
    lvgl_port_unlock();
}

// ---- 采集流程(均按会话 ID 过滤迟到回调) ----
void gui_meas_begin(int session, const card_record_t *card)
{
    (void)card;
    if (lvgl_port_lock(200) != true) return;
    s_session = session;
    lv_label_set_text(s_mark_lbl, "0/10");
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    show_page(PG_MEASURE);
    lvgl_port_unlock();
}

void gui_meas_progress(int session, int i, int total)
{
    if (session != s_session) return;
    if (lvgl_port_lock(200) != true) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", i, total);
    lv_label_set_text(s_mark_lbl, buf);
    lv_bar_set_value(s_bar, i, LV_ANIM_OFF);
    lvgl_port_unlock();
}

void gui_meas_result(const gui_result_t *res)
{
    if (res == NULL || res->session != s_session) return;   // 旧会话回调丢弃
    if (lvgl_port_lock(200) != true) return;
    s_res = *res;
    s_res_valid = true;
    s_fav = false;                    // 重测不得继承收藏/保存假状态
    s_lip_idx = 0;
    fndlist_build(&s_res);
    fill_result_labels();
    show_page(s_meas_origin == PG_CARDS ? PG_CARDS : PG_RESULT);
    lvgl_port_unlock();
}

void gui_meas_failure(int session, const char *reason)
{
    if (session != s_session) return;
    if (lvgl_port_lock(200) != true) return;
    s_fail_origin = s_meas_origin;
    lv_label_set_text(s_fail_title, reason ? reason : "本次采集未通过");
    show_page(PG_FAILURE);
    lvgl_port_unlock();
}

void gui_meas_cancel(int session)
{
    if (session != s_session) return;
    if (lvgl_port_lock(200) != true) return;
    show_page(s_meas_origin);
    lvgl_port_unlock();
}

void gui_meas_saved(int session, bool ok, const char *fname)
{
    if (session != s_session) return;
    if (lvgl_port_lock(200) != true) return;
    s_res.tf_saved = ok;
    if (fname) snprintf(s_res.tf_file, sizeof(s_res.tf_file), "%s", fname);
    fill_result_labels();
    more_refresh();
    lvgl_port_unlock();
}
