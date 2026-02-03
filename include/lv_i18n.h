#ifndef LV_I18N_H
#define LV_I18N_H

#define LV_I18N_LANGUAGE_DEFAULT 0

typedef enum {
    LV_LANGUAGE_EN = 0,
    LV_LANGUAGE_RU = 1,
} lv_language_t;

void lv_i18n_init(lv_language_t lang);
const char * lv_i18n_get_text(const char * key);

#endif
