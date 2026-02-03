#include "ui.h"
#include "ui_events.h"

void ui_Screen1_event_handler(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT) {
            lv_scr_load_anim(ui_Screen2, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        }
    }
}

void ui_Screen2_event_handler(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT) {
            lv_scr_load_anim(ui_Screen3, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
        }
        if (dir == LV_DIR_RIGHT) {
            lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
        }
    }
}
