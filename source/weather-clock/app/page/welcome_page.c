#include "st7789.h"
#include "font.h"
#include "image.h"

void welcome_page_display(void)
{
    const uint16_t color_bg = mkcolor(0, 0, 0);
    st7789_fill_color(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, color_bg);
    st7789_draw_image(30, 10, &img_bubu12);
    st7789_write_string(48, 205, "布布&一二", mkcolor(237, 128, 147), color_bg, &font32_maple_bold);
    st7789_write_string(56, 233, "天气时钟", mkcolor(86, 165, 255), color_bg, &font32_maple_bold);
    st7789_write_string(60, 285, "loading...", mkcolor(255, 255, 255), color_bg, &font24_maple_bold);
}
