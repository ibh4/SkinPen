#pragma once
// 采集流程 —— SkinPen V0.5
// 流程: LOCK_CARD -> LED -> 10 帧采集(帧级清洗) -> QC -> 模型直出 -> 推荐
//       -> TF 保存 -> GUI 结果页
// 采集中禁止切换色卡、禁止再次开始(命令队列忽略)。
// JSONL 协议: start / sample(每帧) / model(模型输出) / match(最近邻对比)
//             / saved / cancelled
// 采集在独立 FreeRTOS 任务中执行,GUI 通过命令队列交互,不阻塞 LVGL。

#include "esp_err.h"
#include <stdbool.h>

// 初始化并启动采集任务(含当天 session_index 已测状态恢复)
esp_err_t measurement_init(void);

// ---- 以下接口可在任意任务上下文调用(内部投递命令) ----
void measurement_cmd_start(void);      // 开始采集当前色卡
void measurement_cmd_prev(void);       // 上一张(采集中忽略)
void measurement_cmd_next(void);       // 下一张(采集中忽略)
void measurement_cmd_select(int idx);  // 跳到指定索引(采集中忽略)
void measurement_cmd_save_retry(void); // TF 保存重试(更多页, 重写最近一条记录)

// 长按测量键取消当前采集(GPIO7 长按,采集中生效);同时退出 AUTO 连测
void measurement_abort(void);

// 是否正在采集
bool measurement_busy(void);

// AUTO 连测开关: 开启后保存一条 -> 自动切下一张 -> 自动开始,循环整库采集
void measurement_set_auto(bool on);
bool measurement_auto(void);
