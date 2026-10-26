// 采集流程实现 —— SkinPen V0.5
// 流程: LOCK_CARD -> LED -> 10 帧采集(帧级清洗) -> QC -> V0.5 模型直出
//       -> recommend_v05 推荐 -> TF 保存 -> GUI 结果页
// 会话 ID: 每次采集 s_session 自增, GUI 回调携带会话, 迟到的旧会话事件被 GUI 忽略
// (取消后不重开结果页、旧结果不覆盖新页面)。
// QC 阈值(自定, 与离线管线清洗口径一致, 记录于交付报告):
//   f1..f8 均值总和 < 200   -> "信号不足"
//   通道 CV 最大值 > 0.10   -> "信号波动较大"
//   有效帧 = 0              -> "传感器读数失败"
//   LED 控制失败            -> "LED 控制失败"
// QC 失败不生成推荐、不写 records.txt, 进失败页可重新测量。
// TF 保存失败时结果页/更多页可重试(MC_SAVE -> measurement_cmd_save_retry)。
// skin_match 最近邻匹配仅保留 JSONL 输出(与模型对比), 不再驱动 GUI。
// LOCK_CARD: 采集开始时复制 activeCard,全程 seq/code 四处一致
// (GUI / TF 记录 / TF 文件名 / USB JSONL 均来自同一份数据)。
#include "measurement.h"

#include "as7341.h"
#include "gui.h"
#include "model.h"
#include "pantone.h"
#include "recommend.h"
#include "sd_manager.h"
#include "skin_match.h"
#include "skin_templates.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MEAS_REPEATS    10      // 连续读帧次数
#define MEAS_LED_MA     8       // LED 电流 8mA
#define MEAS_SETTLE_MS  30      // LED 稳定时间
#define MEAS_GAP_MS     20      // 两次读帧间隔
#define MEAS_AUTO_NEXT_MS 1000  // AUTO 连测: 保存后停留时长
#define MEAS_TASK_STACK 6144

#define QC_SUM8_MIN     200     // f1..f8 均值总和下限(信号不足)
#define QC_CV_MAX       0.10f   // 通道 CV 上限(信号波动较大)

static const char *TAG = "meas";

typedef struct {
    uint8_t cmd;      // MC_*
    int32_t index;    // MC_SELECT 用
} meas_cmd_t;

enum { MC_START, MC_PREV, MC_NEXT, MC_SELECT, MC_SAVE };

static QueueHandle_t s_cmd_q;
static volatile bool s_busy;
static volatile bool s_abort;
static volatile bool s_auto_loop;   // AUTO 连测: 保存后自动切下一张并继续采集

static int       s_session;            // 采集会话计数(每次 run_meas +1)
static sd_meas_t s_last;               // 最近一次测量数据(TF 保存重试用)
static int       s_last_idx = -1;      // 最近测量的色卡索引
static int       s_last_session = -1;  // 最近一次出结果的会话 ID
static bool      s_has_last;           // s_last 有效标志

// ---------------------------------------------------------------- JSONL 输出
// 走 USB CDC(即控制台 stdout)。Mac 端按行解析,以 '{' 开头的行是 JSONL。
static void jsonl_start(const card_record_t *c, const char *ts_iso)
{
    printf("{\"type\":\"start\",\"seq\":%d,\"code\":\"%s\",\"timestamp\":\"%s\"}\n",
           c->seq, c->code, ts_iso);
}

static void jsonl_sample(const card_record_t *c, int repeat, const uint16_t ch[10])
{
    printf("{\"type\":\"sample\",\"seq\":%d,\"code\":\"%s\",\"repeat\":%d,"
           "\"f1\":%u,\"f2\":%u,\"f3\":%u,\"f4\":%u,\"f5\":%u,\"f6\":%u,"
           "\"f7\":%u,\"f8\":%u,\"clear\":%u,\"nir\":%u}\n",
           c->seq, c->code, repeat,
           ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], ch[7], ch[8], ch[9]);
}

static void jsonl_saved(const card_record_t *c, const char *file, const char *status)
{
    printf("{\"type\":\"saved\",\"seq\":%d,\"code\":\"%s\",\"file\":\"%s\",\"status\":\"%s\"}\n",
           c->seq, c->code, file, status);
}

// ---------------------------------------------------------------- 时间戳
// 板上无 RTC,以 main.c 设置的开机基准推进;时区固定 +08:00
static void fill_timestamps(sd_meas_t *m)
{
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(m->timestamp_iso, sizeof(m->timestamp_iso), "%Y-%m-%dT%H:%M:%S+08:00", &tmv);
    strftime(m->date_dir, sizeof(m->date_dir), "%Y%m%d", &tmv);
    strftime(m->hhmmss, sizeof(m->hhmmss), "%H%M%S", &tmv);
    snprintf(m->session_id, sizeof(m->session_id), "%s_A", m->date_dir);
}

// ---------------------------------------------------------------- 帧级清洗
// 与离线管线(皮肤数据/build_final.py)一致:
//   逐通道中位数构成锚帧, 每帧相对欧氏距离 >0.10 视为离群剔除;
//   有效帧 <3 时保留距离最近的 3 帧; 修改 repeat_count 与 repeats
// 清洗后 compute_stats 的 mean/cv 与保存数据均为干净帧
static void clean_repeats(sd_meas_t *m)
{
    const int n = m->repeat_count;
    if (n <= 3) return;

    // 锚帧: 逐通道中位数
    uint16_t med[10] = { 0 };
    for (int ch = 0; ch < 10; ch++) {
        uint16_t tmp[MEAS_REPEATS];
        for (int r = 0; r < n; r++) tmp[r] = m->repeats[r][ch];
        for (int i = 1; i < n; i++) {            // 插入排序(n<=10)
            uint16_t v = tmp[i];
            int j = i - 1;
            while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; }
            tmp[j + 1] = v;
        }
        med[ch] = (tmp[(n - 1) / 2] + tmp[n / 2]) / 2;
    }
    float nmed = 0;
    for (int ch = 0; ch < 10; ch++) nmed += (float)med[ch] * med[ch];
    nmed = sqrtf(nmed);
    if (nmed < 1) return;

    // 每帧相对距离
    float d[MEAS_REPEATS];
    uint8_t keep[MEAS_REPEATS];
    int nkeep = 0;
    for (int r = 0; r < n; r++) {
        float dd = 0;
        for (int ch = 0; ch < 10; ch++) {
            float df = (float)m->repeats[r][ch] - med[ch];
            dd += df * df;
        }
        d[r] = sqrtf(dd) / nmed;
        if (d[r] < 0.10f) keep[nkeep++] = (uint8_t)r;
    }
    if (nkeep < 3) {                             // 放宽: 距离最近 3 帧
        nkeep = 0;
        uint8_t used[MEAS_REPEATS] = { 0 };
        for (int k = 0; k < 3 && k < n; k++) {
            int bj = -1;
            for (int r = 0; r < n; r++)
                if (!used[r] && (bj < 0 || d[r] < d[bj])) bj = r;
            used[bj] = 1;
            keep[nkeep++] = (uint8_t)bj;
        }
    }
    if (nkeep == n) return;
    // 压缩 repeats(保序: keep 天然升序)
    for (int i = 0; i < nkeep; i++)
        memcpy(m->repeats[i], m->repeats[keep[i]], sizeof(uint16_t) * 10);
    m->repeat_count = nkeep;
    ESP_LOGI(TAG, "帧清洗: %d -> %d 帧", n, nkeep);
}

// ---------------------------------------------------------------- 统计
static void compute_stats(sd_meas_t *m)
{
    const int n = m->repeat_count;
    m->cv_max = 0;
    for (int ch = 0; ch < 10; ch++) {
        double sum = 0;
        for (int r = 0; r < n; r++) sum += m->repeats[r][ch];
        double mean = n ? sum / n : 0;
        double var = 0;
        for (int r = 0; r < n; r++) {
            double d = m->repeats[r][ch] - mean;
            var += d * d;
        }
        var = n ? var / n : 0;
        double sd = sqrt(var);
        double cv = (mean > 1.0) ? sd / mean : 0;   // 均值过低时 CV 无意义
        m->mean[ch] = (uint16_t)(mean + 0.5);
        m->std[ch] = (float)sd;
        m->cv[ch] = (float)cv;
        if (cv > m->cv_max) m->cv_max = (float)cv;
    }
}

// ---------------------------------------------------------------- TF 保存
// 保存成功返回 true 并回填 fname(basename); 失败返回 false
static bool save_to_sd(sd_meas_t *m, char *fname_out, size_t fname_len)
{
    if (!sdman_ready()) {
        ESP_LOGE(TAG, "TF 未就绪,保存跳过");
        if (fname_out && fname_len) fname_out[0] = '\0';
        return false;
    }
    char path[128] = { 0 };
    esp_err_t err = sdman_save(m, path, sizeof(path));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TF 保存失败: %s", esp_err_to_name(err));
        if (fname_out && fname_len) fname_out[0] = '\0';
        return false;
    }
    const char *fname = path;
    const char *slash = strrchr(path, '/');
    if (slash) fname = slash + 1;
    snprintf(m->status, sizeof(m->status), "PASS");
    sdman_session_index_append(m->date_dir, m->seq, m->code, m->status, fname);
    if (fname_out && fname_len) snprintf(fname_out, fname_len, "%s", fname);
    return true;
}

// ---------------------------------------------------------------- 采集主流程
static void run_meas(void)
{
    // LOCK_CARD: 复制当前色卡,采集期间 GUI 导航被忽略,activeCard 不变
    const card_record_t *cur = pantone_current();
    if (cur == NULL || pantone_count() == 0) {
        int session = ++s_session;
        ESP_LOGE(TAG, "色卡库未加载,无法采集");
        gui_meas_failure(session, "色卡库未加载");   // 失败页可见反馈,可返回
        return;
    }
    card_record_t active = *cur;
    int cur_idx = pantone_index();

    s_busy = true;
    s_abort = false;
    s_has_last = false;
    int session = ++s_session;

    sd_meas_t m;
    memset(&m, 0, sizeof(m));
    m.seq = (uint32_t)active.seq;
    snprintf(m.code, sizeof(m.code), "%s", active.code);
    snprintf(m.name, sizeof(m.name), "%s", active.name_en);
    m.gain = 16;
    m.atime = 29;
    m.astep = 599;
    m.led_ma = MEAS_LED_MA;
    m.led_on = true;
    fill_timestamps(&m);

    gui_meas_begin(session, &active);
    jsonl_start(&active, m.timestamp_iso);
    ESP_LOGI(TAG, "开始采集 seq=%d code=%s session=%d", active.seq, active.code, session);

    // LED 开,稳定后连续读帧(共享 I2C 总线偶发瞬态繁忙, 重试 1 次)
    bool led_ok = false;
    for (int t = 0; t < 2 && !led_ok; t++) {
        led_ok = (as7341_set_led_current(MEAS_LED_MA) == ESP_OK &&
                  as7341_led_set(true) == ESP_OK);
        if (!led_ok) vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!led_ok) {
        as7341_led_set(false);
        gui_meas_failure(session, "LED 控制失败");
        s_busy = false;
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(MEAS_SETTLE_MS));

    int ok = 0;
    for (int i = 0; i < MEAS_REPEATS; i++) {
        if (s_abort) break;                        // 页面取消/长按取消
        as7341_frame_t f;
        if (as7341_read_frame(&f) == ESP_OK) {
            uint16_t ch[10] = { f.f1, f.f2, f.f3, f.f4, f.f5, f.f6, f.f7, f.f8, f.clear, f.nir };
            memcpy(m.repeats[ok], ch, sizeof(ch));
            jsonl_sample(&active, ok + 1, ch);
            ok++;
        } else {
            ESP_LOGW(TAG, "第 %d 次读帧失败,跳过", i + 1);
        }
        gui_meas_progress(session, i + 1, MEAS_REPEATS);
        vTaskDelay(pdMS_TO_TICKS(MEAS_GAP_MS));
    }
    as7341_led_set(false);
    m.repeat_count = ok;

    // ---- 取消 ----
    if (s_abort) {
        printf("{\"type\":\"cancelled\",\"seq\":%d,\"code\":\"%s\"}\n", active.seq, active.code);
        gui_meas_cancel(session);
        ESP_LOGW(TAG, "采集已取消 seq=%d session=%d", active.seq, session);
        s_busy = false;
        return;
    }

    // ---- 有效帧 ----
    if (ok == 0) {
        gui_card_mark(cur_idx, GUI_CARD_FAIL);
        jsonl_saved(&active, "", "FAIL");
        gui_meas_failure(session, "传感器读数失败");
        s_busy = false;
        return;
    }

    clean_repeats(&m);            // 帧级离群剔除(与离线管线一致)
    compute_stats(&m);
    fill_timestamps(&m);          // 文件名时间戳取保存时刻

    // ---- QC ----
    int sum8 = 0;
    for (int i = 0; i < 8; i++) sum8 += m.mean[i];
    if (sum8 < QC_SUM8_MIN) {
        gui_card_mark(cur_idx, GUI_CARD_FAIL);
        jsonl_saved(&active, "", "QC_LOW");
        gui_meas_failure(session, "信号不足");
        s_busy = false;
        return;
    }
    if (m.cv_max > QC_CV_MAX) {
        gui_card_mark(cur_idx, GUI_CARD_FAIL);
        jsonl_saved(&active, "", "QC_CV");
        gui_meas_failure(session, "信号波动较大");
        s_busy = false;
        return;
    }

    // ---- V0.5 模型直出: 光谱 -> RGB/冷暖/肤深 ----
    model_out_t mo;
    if (model_predict(m.mean, &mo) != 0) {
        gui_card_mark(cur_idx, GUI_CARD_FAIL);
        jsonl_saved(&active, "", "FAIL");
        gui_meas_failure(session, "模型计算失败");
        s_busy = false;
        return;
    }

    // ---- 推荐(粉底 3 档 + 口红候选) ----
    rec_item_t fnd[3];
    int fnd_best = 0;
    rec_item_t lip[GUI_LIP_MAX];
    int lip_n = 0;
    recommend_v05(mo.rgb565, mo.cool, mo.dep16, fnd, &fnd_best, lip, GUI_LIP_MAX, &lip_n);

    // ---- skin_match 最近邻(仅 JSONL 对比, 不驱动 GUI) ----
    float match_dist = -1;
    int match_idx = skin_match_find(m.mean, &match_dist);
    if (match_idx >= 0) {
        printf("{\"type\":\"match\",\"meas_seq\":%d,\"meas_code\":\"%s\","
               "\"match_seq\":%d,\"match_code\":\"%s\",\"dist\":%.4f}\n",
               active.seq, active.code,
               g_skin_tpl[match_idx].seq, g_skin_tpl[match_idx].code, match_dist);
    } else {
        printf("{\"type\":\"match\",\"meas_seq\":%d,\"meas_code\":\"%s\",\"match_code\":\"\","
               "\"dist\":-1}\n", active.seq, active.code);
    }

    // ---- 模型输出 JSONL ----
    printf("{\"type\":\"model\",\"seq\":%d,\"code\":\"%s\",\"r\":%u,\"g\":%u,\"b\":%u,"
           "\"rgb565\":\"#%04X\",\"tone\":\"%s\",\"dep16\":%d,\"cv_max\":%.4f}\n",
           active.seq, active.code, mo.r, mo.g, mo.b, mo.rgb565,
           mo.cool ? "C" : "W", mo.dep16, m.cv_max);

    // ---- TF 保存 ----
    snprintf(m.status, sizeof(m.status), "PASS");
    char fname[64] = { 0 };
    bool saved = save_to_sd(&m, fname, sizeof(fname));
    if (!saved) snprintf(m.status, sizeof(m.status), "FAIL");
    jsonl_saved(&active, saved ? fname : "", m.status);

    // ---- GUI 结果 ----
    gui_result_t res;
    memset(&res, 0, sizeof(res));
    res.session = session;
    res.r = mo.r;
    res.g = mo.g;
    res.b = mo.b;
    res.rgb565 = mo.rgb565;
    res.cool = mo.cool;
    res.dep16 = mo.dep16;
    res.cv_max = m.cv_max;
    for (int i = 0; i < 8; i++) res.spec[i] = m.mean[i];   // f1..f8 均值(光谱曲线)
    res.tf_saved = saved;
    snprintf(res.tf_file, sizeof(res.tf_file), "%s", saved ? fname : "");
    memcpy(res.fnd, fnd, sizeof(fnd));
    res.fnd_best = fnd_best;
    memcpy(res.lip, lip, sizeof(lip));
    res.lip_n = lip_n;
    gui_meas_result(&res);        // 工程模式进 cards 页, 普通模式进结果页

    // 记录最近结果(TF 保存失败时更多页可重试)
    s_last = m;
    s_last_idx = cur_idx;
    s_last_session = session;
    s_has_last = true;

    s_busy = false;

    // ---- AUTO 连测: 停留 ~1s 后自动切下一张继续 ----
    // 若停留期间用户已有操作(命令队列非空), 跳过自动切换交由命令循环处理
    if (s_auto_loop) {
        vTaskDelay(pdMS_TO_TICKS(MEAS_AUTO_NEXT_MS));
        if (!s_abort && uxQueueMessagesWaiting(s_cmd_q) == 0) {
            pantone_next();                       // 末尾自动环绕回第一张
            gui_set_card(pantone_index(), pantone_count(), pantone_current());
            meas_cmd_t c = { MC_START, 0 };
            xQueueSend(s_cmd_q, &c, 0);
        }
    }
}

// ---------------------------------------------------------------- TF 保存重试
// 更多页"重新保存": 对最近一次测量重新执行 TF 写入
static void save_retry(void)
{
    if (!s_has_last) {
        ESP_LOGW(TAG, "无最近测量数据,保存重试忽略");
        return;
    }
    char fname[64] = { 0 };
    bool saved = save_to_sd(&s_last, fname, sizeof(fname));
    if (!saved) snprintf(s_last.status, sizeof(s_last.status), "FAIL");
    printf("{\"type\":\"saved\",\"seq\":%lu,\"code\":\"%s\",\"file\":\"%s\",\"status\":\"%s\",\"retry\":true}\n",
           (unsigned long)s_last.seq, s_last.code, saved ? fname : "",
           saved ? "PASS" : "FAIL");
    gui_meas_saved(s_last_session, saved, saved ? fname : "");
}

// ---------------------------------------------------------------- session 恢复
static int index_restore(uint32_t seq, const char *code, const char *status,
                         const char *filename, void *user)
{
    (void)filename;
    int total = pantone_count();
    for (int i = 0; i < total; i++) {
        const card_record_t *c = pantone_get(i);
        if (c && (uint32_t)c->seq == seq) {
            gui_card_mark(i, strcmp(status, "PASS") == 0 ? GUI_CARD_PASS : GUI_CARD_FAIL);
            (void)code;
            break;
        }
    }
    return 0;   // 继续遍历
}

// ---------------------------------------------------------------- 命令任务
static void meas_task(void *arg)
{
    for (;;) {
        meas_cmd_t c;
        if (xQueueReceive(s_cmd_q, &c, portMAX_DELAY) != pdTRUE) continue;
        switch (c.cmd) {
        case MC_PREV:
            if (!s_busy) {
                pantone_prev();
                gui_set_card(pantone_index(), pantone_count(), pantone_current());
            }
            break;
        case MC_NEXT:
            if (!s_busy) {
                pantone_next();
                gui_set_card(pantone_index(), pantone_count(), pantone_current());
            }
            break;
        case MC_SELECT:
            if (!s_busy) {
                pantone_select(c.index);
                gui_set_card(pantone_index(), pantone_count(), pantone_current());
            }
            break;
        case MC_SAVE:
            if (!s_busy) save_retry();
            break;
        case MC_START:
        default:
            if (!s_busy) run_meas();
            break;
        }
    }
}

esp_err_t measurement_init(void)
{
    s_cmd_q = xQueueCreate(8, sizeof(meas_cmd_t));
    if (s_cmd_q == NULL) return ESP_ERR_NO_MEM;

    // 恢复当天已测状态(session_index)
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char date_dir[9];
    strftime(date_dir, sizeof(date_dir), "%Y%m%d", &tmv);
    int n = sdman_session_index_read(date_dir, index_restore, NULL);
    if (n > 0) ESP_LOGI(TAG, "恢复当天 session: %d 条记录", n);

    BaseType_t r = xTaskCreate(meas_task, "meas", MEAS_TASK_STACK, NULL, 5, NULL);
    return r == pdPASS ? ESP_OK : ESP_FAIL;
}

void measurement_cmd_start(void) { meas_cmd_t c = { MC_START, 0 }; xQueueSend(s_cmd_q, &c, 0); }
void measurement_cmd_prev(void)  { meas_cmd_t c = { MC_PREV, 0 };  xQueueSend(s_cmd_q, &c, 0); }
void measurement_cmd_next(void)  { meas_cmd_t c = { MC_NEXT, 0 };  xQueueSend(s_cmd_q, &c, 0); }
void measurement_cmd_select(int idx) { meas_cmd_t c = { MC_SELECT, idx }; xQueueSend(s_cmd_q, &c, 0); }
void measurement_cmd_save_retry(void) { meas_cmd_t c = { MC_SAVE, 0 }; xQueueSend(s_cmd_q, &c, 0); }
// 取消当前采集(采集页"取消"/长按测量键); 同时退出 AUTO 连测(避免取消后又自动开始下一张)
void measurement_abort(void)
{
    s_abort = true;
    if (s_auto_loop) {
        s_auto_loop = false;
    }
}
bool measurement_busy(void) { return s_busy; }
void measurement_set_auto(bool on)
{
    s_auto_loop = on;
    ESP_LOGI(TAG, "AUTO 连测 %s", on ? "开启" : "关闭");
}
bool measurement_auto(void) { return s_auto_loop; }
