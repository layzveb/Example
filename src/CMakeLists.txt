SET(SOURCES screens/ui_Screen1.c
    screens/ui_Screen2.c
    screens/ui_Screen3.c
    screens/ui_Screen4.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_1939720481.c
    images/ui_img_2122670701.c
    images/ui_img_1360404416.c
    images/ui_img_277349689.c
    images/ui_img_377560965.c
    fonts/ui_font_Font14.c
    fonts/ui_font_Font12.c
    fonts/ui_font_Font10.c
    fonts/ui_font_Font16.c
    fonts/ui_font_Font20.c
    fonts/ui_font_FontBold30.c
    fonts/ui_font_Font18.c
    fonts/ui_font_FontBold16.c
    fonts/ui_font_FontBold14.c
    fonts/ui_font_FontBold25.c)

add_library(ui ${SOURCES})
