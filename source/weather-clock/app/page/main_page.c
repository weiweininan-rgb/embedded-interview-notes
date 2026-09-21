#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "rtc.h"
#include "st7789.h"
#include "font.h"
#include "image.h"
#include "page.h"
#include "app.h"
#include "esp_at.h" // --- 新增：引入 WiFi 状态检查函数 ---

// --- 1. 定义全局 UI 主题色 ---
// mkcolor(R, G, B) 将红绿蓝转换为屏幕识别的 16位颜色 (RGB565格式)
static const uint16_t color_bg_time = mkcolor(248, 248, 248);     // 上方时间区域背景色：近乎纯白
static const uint16_t color_bg_inner = mkcolor(136, 217, 234);    // 左下室内区域背景色：清爽的天蓝色
static const uint16_t color_bg_outdoor = mkcolor(254, 135, 75);   // 右下室外区域背景色：温暖的橙色

/**
 * @brief  主页面静态框架绘制函数 (只在刚进入主页时调用一次)
 */
void main_page_display(void)
{
    // 先用纯黑色清空整个屏幕
    st7789_fill_color(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, mkcolor(0, 0, 0));
    
    // ==========================================
    // 区块 1：上方时间与 WiFi 信息区
    // ?? 小技巧：使用 do {...} while(0) 是 C语言大牛常用手法，用来把一块逻辑包起来，方便折叠和阅读，没有实际循环作用。
    do {
        // 画出上方的大白底色块 (坐标从 x=15, y=15 到 x=224, y=154)
        st7789_fill_color(15, 15, 224, 154, color_bg_time);
        
        // 画固定的 WiFi 小图标
        st7789_draw_image(23, 20, &icon_wifi);
        // 调用重绘函数，把 WiFi 名字写上去
       
		if (wifi_is_connected()) {
            // 如果连上了，显示预设的 WiFi 名字
            main_page_redraw_wifi_ssid(WIFI_SSID);
        } else {
            // 如果没连上，显示报错信息
            main_page_redraw_wifi_ssid("wifi lost");
        }
				
//				main_page_redraw_wifi_ssid(WIFI_SSID);
//        // 占位符：先画上初始的 --:-- 时间
				
        st7789_write_string(25, 42, "--:--", mkcolor(0, 0, 0), color_bg_time, &font76_maple_extrabold);
        // 占位符：画上初始的日期
        st7789_write_string(35, 121, "----/--/-- 星期--", mkcolor(143, 143, 143), color_bg_time, &font20_maple_bold);
    } while (0);
    
    // ==========================================
    // 区块 2：左下角室内环境区
    do {
        // 画出左下的天蓝色底块
        st7789_fill_color(15, 165, 114, 304, color_bg_inner);
        // 写上静态标题 "室内环境"
        st7789_write_string(19, 170, "室内环境", mkcolor(0, 0, 0), color_bg_inner, &font24_maple_semibold);
        
        // ?? 案发现场 1：这里原作者在 x=86, y=191 处写了一个静态的字母 "C" 代表温度单位！
        st7789_write_string(86, 191, "C", mkcolor(0, 0, 0), color_bg_inner, &font32_maple_bold);
        // 写上静态的 "%" 代表湿度单位
        st7789_write_string(91, 262, "%", mkcolor(0, 0, 0), color_bg_inner, &font32_maple_bold);
        
        // 占位符：先填上 999.9 代表还没读到传感器数据
        main_page_redraw_inner_temperature(999.9f);
        main_page_redraw_inner_humidity(999.9f);
    } while (0);
    
    // ==========================================
    // 区块 3：右下角室外天气区
    do {
        // 画出右下的橙色底块
        st7789_fill_color(125, 165, 224, 304, color_bg_outdoor);
        
        // ?? 案发现场 2：室外温度的单位，同样只写了一个静态的字母 "C"
        st7789_write_string(192, 189, "C", mkcolor(0, 0, 0), color_bg_outdoor, &font32_maple_bold);
        // 画上一个固定的温度计小图标
        st7789_draw_image(139, 239, &icon_wenduji);
        
        // 填上初始站位数据
        main_page_redraw_outdoor_city("北京");
        main_page_redraw_outdoor_temperature(999.9f);
        main_page_redraw_outdoor_weather_icon(-1);
    } while (0);
}

// ==============================================================
// 以下全部是“局部重绘 (Redraw)”函数，由主循环在后台定时调用更新
// ==============================================================

/**
 * @brief 更新 WiFi 名称
 */
void main_page_redraw_wifi_ssid(const char *ssid)
{
    char str[21];
    // %20s 表示最多显示 20 个字符，防止名字太长撑爆屏幕
    snprintf(str, sizeof(str), "%20s", ssid);
    st7789_write_string(50, 23, str, mkcolor(143, 143, 143), color_bg_time, &font16_maple);
}

/**
 * @brief 更新数字时钟 (带有闪烁的冒号特效)
 */
void main_page_redraw_time(rtc_date_time_t *time)
{
    char str[6];
    // ?? 视觉魔法：如果当前秒数是偶数，冒号显示 ":"；如果是奇数，冒号变成空格 " "
    // 这样人眼看过去，时钟中间的冒号就像在一秒一秒地闪烁！
    char comma = (time->second % 2 == 0) ? ':' : ' ';
    
    // 拼接时间字符串，例如 "10:35" 或 "10 35"
    snprintf(str, sizeof(str), "%02u%c%02u", time->hour, comma, time->minute);
    
    // 用 76号 的超级大字体打印时间
    st7789_write_string(25, 42, str, mkcolor(0, 0, 0), color_bg_time, &font76_maple_extrabold);
}

/**
 * @brief 更新日期与星期
 */
void main_page_redraw_date(rtc_date_time_t *date)
{
    char str[18];
    // 使用三目运算符，把数字的 1~7 转换成汉字的 "一" 到 "天"
    snprintf(str, sizeof(str), "%04u/%02u/%02u 星期%s", date->year, date->month, date->day,
        date->weekday == 1 ? "一" :
        date->weekday == 2 ? "二" :
        date->weekday == 3 ? "三" :
        date->weekday == 4 ? "四" :
        date->weekday == 5 ? "五" :
        date->weekday == 6 ? "六" :
        date->weekday == 7 ? "天" : "X");
    st7789_write_string(35, 121, str, mkcolor(143, 143, 143), color_bg_time, &font20_maple_bold);
}

/**
 * @brief 更新室内温度
 */
void main_page_redraw_inner_temperature(float temperature)
{
    char str[3] = {'-', '-'};
    // 合法性校验：如果温度在 -10 到 100 度之间，才转换。否则显示 "--"
    if (temperature > -10.0f && temperature <= 100.0f)
        snprintf(str, sizeof(str), "%2.0f", temperature); // %2.0f 表示不要小数点，四舍五入到整数
    st7789_write_string(30, 192, str, mkcolor(0, 0, 0), color_bg_inner, &font54_maple_semibold);
}
    
/**
 * @brief 更新室内湿度
 */
void main_page_redraw_inner_humidity(float humidity)
{
    char str[3];
    // 合法性校验：湿度应该在 0~100% 之间
    if (humidity > 0.0f && humidity <= 99.99f)
        snprintf(str, sizeof(str), "%2.0f", humidity);
    st7789_write_string(25, 239, str, mkcolor(0, 0, 0), color_bg_inner, &font64_maple_extrabold);
}

/**
 * @brief 更新室外城市名
 */
void main_page_redraw_outdoor_city(const char *city)
{
    char str[9];
    snprintf(str, sizeof(str), "%s", city);
    st7789_write_string(127, 170, str, mkcolor(0, 0, 0), color_bg_outdoor, &font24_maple_semibold);
}

/**
 * @brief 更新室外温度
 */
void main_page_redraw_outdoor_temperature(float temperature)
{
    char str[3] = {'-', '-'};
    if (temperature > -10.0f && temperature <= 100.0f)
        snprintf(str, sizeof(str), "%2.0f", temperature);
    st7789_write_string(135, 190, str, mkcolor(0, 0, 0), color_bg_outdoor, &font54_maple_bold);
}

/**
 * @brief 更新室外天气图标 (重点逻辑！)
 * @param code 知心天气API返回的天气代码 (0代表晴，4代表多云...)
 */
void main_page_redraw_outdoor_weather_icon(const int code)
{
    const image_t *icon;
    
    // 根据 API 代码，映射到本地的图片资源数组
    if (code == 0 || code == 2 || code == 38)
        icon = &icon_qing;           // 晴天
    else if (code == 1 || code == 3)
        icon = &icon_yueliang;       // 晚上晴天 (月亮)
    else if (code == 4 || code == 9)
        icon = &icon_yintian;        // 阴天
    else if (code == 5 || code == 6 || code == 7 || code == 8)
        icon = &icon_duoyun;         // 多云
    else if (code == 10 || code == 13 || code == 14 || code == 15 || code == 16 || code == 17 || code == 18 || code == 19)
        icon = &icon_zhongyu;        // 中雨/各类雨
    else if (code == 11 || code == 12)
        icon = &icon_leizhenyu;      // 雷阵雨
    else if (code == 20 || code == 21 || code == 22 || code == 23 || code == 24 || code == 25)
        icon = &icon_zhongxue;       // 中雪/各类雪
    else // 扬沙、龙卷风等极端天气，或者发生未知错误 (-1)
        icon = &icon_na;             // 显示 N/A 暂无图片
        
    // 选好图片后，在右下角的指定坐标画出来
    st7789_draw_image(166, 240, icon);
}
