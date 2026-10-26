#include "qmi8658_tile.h"

static lv_obj_t *list;
void qmi8658_tile_init(lv_obj_t *parent)
{
    /*Create a list*/
    list = lv_list_create(parent);
    lv_obj_t *lable =  lv_label_create(parent);
    lv_obj_set_style_text_font(lable, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_label_set_text(lable, "QMI8658");
    lv_obj_align(lable, LV_ALIGN_TOP_MID, 0, 3);

    lv_obj_set_size(list, lv_pct(100), lv_pct(90));
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *list_item;

    list_item = lv_list_add_btn(list, NULL, "Accel_x");
    lv_obj_t *label_accel_x = lv_label_create(list_item);
    lv_label_set_text(label_accel_x, "----");

    list_item = lv_list_add_btn(list, NULL, "Accel_y");
    lv_obj_t *label_accel_y = lv_label_create(list_item);
    lv_label_set_text(label_accel_y, "----");

    list_item = lv_list_add_btn(list, NULL, "Accel_z");
    lv_obj_t *label_accel_z = lv_label_create(list_item);
    lv_label_set_text(label_accel_z, "----");

    list_item = lv_list_add_btn(list, NULL, "Gyro_x");
    lv_obj_t *label_gyro_x = lv_label_create(list_item);
    lv_label_set_text(label_gyro_x, "----");

    list_item = lv_list_add_btn(list, NULL, "Gyro_y");
    lv_obj_t *label_gyro_y = lv_label_create(list_item);
    lv_label_set_text(label_gyro_y, "----");

    list_item = lv_list_add_btn(list, NULL, "Gyro_z");
    lv_obj_t *label_gyro_z = lv_label_create(list_item);
    lv_label_set_text(label_gyro_z, "----");

    list_item = lv_list_add_btn(list, NULL, "IMU_Temp");
    lv_obj_t *label_imu_temp = lv_label_create(list_item);
    lv_label_set_text(label_imu_temp, "--- C");
}