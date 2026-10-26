# SkinPen V0.5 main 组件
idf_component_register(SRCS "main.c" "as7341.c" "sd_manager.c" "pantone.c" "pantone_builtin.c"
                            "gui.c" "measurement.c" "model.c" "skin_match.c" "recommend.c"
                            "font_zh_14.c" "font_zh_14b.c" "font_zh_16.c"
                            "font_zh_18.c" "font_zh_20.c" "font_zh_22.c" "font_zh_24.c"
                       INCLUDE_DIRS "."
                       REQUIRES "esp_bsp" "lvgl" "esp_lvgl_port" "nvs_flash" "fatfs" "driver" "button")

# lv_font_conv 生成的字库模板默认 include "lvgl/lvgl.h", ESP-IDF lvgl 组件头文件路径为 "lvgl.h"
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
