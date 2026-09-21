#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cpu_tick.h"
#include "rtc.h"
#include "aht20.h"
#include "esp_at.h"
#include "weather.h"
#include "page.h"
#include "app.h"

// --- 1. 时间单位转换宏定义 ---
// 这些宏是为了让代码更具可读性，将所有时间单位统一转换为毫秒
#define MILLISECONDS(x) (x)
#define SECONDS(x)      MILLISECONDS((x) * 1000)
#define MINUTES(x)      SECONDS((x) * 60)
#define HOURS(x)        MINUTES((x) * 60)
#define DAYS(x)          HOURS((x) * 24)

// --- 2. 各任务更新频率配置 ---
#define TIME_SYNC_INTERVAL          DAYS(1)      // 网络对时：每天 1 次
#define WIFI_UPDATE_INTERVAL        SECONDS(5)   // WiFi状态检查：每 5 秒 1 次
#define TIME_UPDATE_INTERVAL        SECONDS(1)   // 屏幕时间更新：每 1 秒 1 次
#define INNER_UPDATE_INTERVAL       SECONDS(3)   // 室内温湿度读取：每 3 秒 1 次
#define OUTDOOR_UPDATE_INTERVAL     MINUTES(1)   // 外部天气抓取：每 1 分钟 1 次

// --- 3. 倒计时变量 ---
// 这些变量由底层的定时器中断不断递减，等于 0 时代表该执行任务了
static uint32_t time_sync_delay;
static uint32_t wifi_update_delay;
static uint32_t time_update_delay;
static uint32_t inner_update_delay;
static uint32_t outdoor_update_delay;

/**
 * @brief  定时器回调函数 (由底层硬件定时器每 1 毫秒调用一次)
 * @note   核心作用是让所有任务的倒计时减 1
 */
static void cpu_periodic_callback(void)
{
    if (time_sync_delay > 0)
        time_sync_delay--;
    if (wifi_update_delay > 0)
        wifi_update_delay--;
    if (time_update_delay > 0)
        time_update_delay--;
    if (inner_update_delay > 0)
        inner_update_delay--;
    if (outdoor_update_delay > 0)
        outdoor_update_delay--;
}

/**
 * @brief  初始化主循环
 * @note   将回调函数注册到底层定时器中
 */
void main_loop_init(void)
{
    cpu_register_periodic_callback(cpu_periodic_callback);
}

// ==========================================
//           各个具体任务的实现
// ==========================================

/**
 * @brief  任务：通过网络同步时间
 */
static void time_sync(void)
{
    // 如果倒计时还没到 0，直接退出 (非阻塞)
    if (time_sync_delay > 0)
        return;
    
    // 重新装填倒计时
    time_sync_delay = TIME_SYNC_INTERVAL;
    
    esp_date_time_t esp_date = { 0 };
    // 向 WiFi 模块索要时间
    if (!esp_at_sntp_get_time(&esp_date))
    {
        printf("[SNTP] get time failed\n");
        time_sync_delay = SECONDS(1); // 失败了就 1 秒后再试
        return;
    }
    
    // 基础校验：如果年份小于 2000，说明数据无效
    if (esp_date.year < 2000)
    {
        printf("[SNTP] invalid date formate\n");
        time_sync_delay = SECONDS(1);
        return;
    }
    
    printf("[SNTP] sync time: %04u-%02u-%02u %02u:%02u:%02u (%d)\n",
        esp_date.year, esp_date.month, esp_date.day,
        esp_date.hour, esp_date.minute, esp_date.second, esp_date.weekday);
    
    // 将网络拿到的时间转换并写入本地硬件 RTC
    rtc_date_time_t rtc_date = { 0 };
    rtc_date.year = esp_date.year;
    rtc_date.month = esp_date.month;
    rtc_date.day = esp_date.day;
    rtc_date.hour = esp_date.hour;
    rtc_date.minute = esp_date.minute;
    rtc_date.second = esp_date.second;
    rtc_date.weekday = esp_date.weekday;
    rtc_set_time(&rtc_date);
    
    // 强制立刻更新屏幕时间
    time_update_delay = 0;
}

/**
 * @brief  任务：更新 WiFi 连接状态
 */
static void wifi_update(void)
{
    static esp_wifi_info_t last_info = { 0 }; // 记住上次的状态

    if (wifi_update_delay > 0)
        return;
    
    wifi_update_delay = WIFI_UPDATE_INTERVAL;
    
    esp_wifi_info_t info = { 0 };
    if (!esp_at_get_wifi_info(&info))
    {
        printf("[AT] wifi info get failed\n");
			
				main_page_redraw_wifi_ssid("wifi lost"); 
			// --- 增加这一行，确保通信异常时屏幕也会更新 ---
        return;
    }
    
	//新增部分	
		
		// ==========================================
    // --- 新增：断线自动重连核心逻辑 ---
    // ==========================================
    if (info.connected == false) 
    {
        printf("[WIFI] Try to reconnect...\n");
       // main_page_redraw_wifi_ssid("connecting..."); // 屏幕提示正在重连
        
        // 重新发送账号密码进行连接！
        if (esp_at_connect_wifi(WIFI_SSID, WIFI_PASSWD, NULL)) 
        {
            // 如果连上了，为了安全起见，重新激活一下网络对时服务
            esp_at_sntp_init(); 
            // 连上后，立刻触发一次天气和时间的强制更新！
            outdoor_update_delay = 0; 
            time_sync_delay = 0;
        }
        
        // ?? 极其重要的优化：如果没连上，把下一次检查的时间拉长到 15 秒！
        // 防止单片机疯狂重连，导致系统发烫卡顿
        wifi_update_delay = SECONDS(5); 
        return;
    }
    // ==========================================
		
    // 使用 memcmp 比较：如果所有信息都和上次一样，就不刷新屏幕
    if (memcmp(&info, &last_info, sizeof(esp_wifi_info_t)) == 0)
    {
        return;
    }
    
    if (last_info.connected == info.connected)
    {
        return;
    }
    
    // 连接状态发生变化，更新屏幕和控制台
    if (info.connected)
    {
        printf("[WIFI] connected to %s\n", info.ssid);
        printf("[WIFI] SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\n",
                info.ssid, info.bssid, info.channel, info.rssi);
        main_page_redraw_wifi_ssid(info.ssid);
    }
    else
    {
        printf("[WIFI] disconnected from %s\n", last_info.ssid);
        main_page_redraw_wifi_ssid("wifi lost");
    }
    
    // 使用 memcpy 把新状态覆盖进“上次状态”记录本
    memcpy(&last_info, &info, sizeof(esp_wifi_info_t));
}

/**
 * @brief  任务：更新屏幕上的时间显示
 */
static void time_update(void)
{
    static rtc_date_time_t last_date = { 0 };
    
    if (time_update_delay > 0)
        return;
    
    time_update_delay = TIME_UPDATE_INTERVAL;
    
    rtc_date_time_t date;
    rtc_get_time(&date); // 从硬件 RTC 获取时间
    
    if (date.year < 2020)
    {
        return;
    }
    
    // 如果秒数没变，不刷新屏幕
    if (memcmp(&date, &last_date, sizeof(rtc_date_time_t)) == 0)
    {
        return;
    }
    
    memcpy(&last_date, &date, sizeof(rtc_date_time_t));
    main_page_redraw_time(&date); // 叫屏幕重绘
    main_page_redraw_date(&date);
}

/**
 * @brief  任务：读取 AHT20 室内温湿度并更新屏幕
 */
static void inner_update(void)
{
    static float last_temperature, last_humidity;
    
    if (inner_update_delay > 0)
        return;
    
    inner_update_delay = INNER_UPDATE_INTERVAL;
    
    if (!aht20_start_measurement())
    {
        printf("[AHT20] start measurement failed\n");
        return;
    }
    
    if (!aht20_wait_for_measurement())
    {
        printf("[AHT20] wait for measurement failed\n");
        return;
    }
    
    float temperature = 0.0f, humidity = 0.0f;
    
    if (!aht20_read_measurement(&temperature, &humidity))
    {
        printf("[AHT20] read measurement failed\n");
        return;
    }
    
    // 浮点数可以直接用 == 比较，如果没变就不刷新屏幕
    if (temperature == last_temperature && humidity == last_humidity)
    {
        return;
    }
    
    last_temperature = temperature;
    last_humidity = humidity;
    
    printf("[AHT20] Temperature: %.1f, Humidity: %.1f\n", temperature, humidity);
    main_page_redraw_inner_temperature(temperature);
    main_page_redraw_inner_humidity(humidity);
}

/**
 * @brief  任务：通过 HTTP 获取天气并更新屏幕
 */
static void outdoor_update(void)
{
    static weather_info_t last_weather = { 0 };
    
    if (outdoor_update_delay > 0)
        return;
    
    outdoor_update_delay = OUTDOOR_UPDATE_INTERVAL;
    
    weather_info_t weather = { 0 };
    // 指定知心天气 API 的请求地址
    const char *weather_url = "https://api.seniverse.com/v3/weather/now.json?key=YOUR_API_KEY&location=beijing&language=en&unit=c";
    
    // 发送 HTTP GET 请求
    const char *weather_http_response = esp_at_http_get(weather_url);
    if (weather_http_response == NULL)
    {
        printf("[WEATHER] http error\n");
        return;
    }
    
    // 解析返回的 JSON 字符串
    if (!parse_seniverse_response(weather_http_response, &weather))
    {
        printf("[WEATHER] parse failed\n");
        return;
    }
    
    if (memcmp(&last_weather, &weather, sizeof(weather_info_t)) == 0)
    {
        return;
    }
    
    memcpy(&last_weather, &weather, sizeof(weather_info_t));
    printf("[WEATHER] %s, %s, %.1f\n", weather.city, weather.weather, weather.temperature);
    
    main_page_redraw_outdoor_temperature(weather.temperature);
    main_page_redraw_outdoor_weather_icon(weather.weather_code);
}

/**
 * @brief  全局主循环调度器 (会被 main.c 里的 while(1) 疯狂调用)
 */
void main_loop(void)
{
    time_sync();
    wifi_update();
    time_update();
    inner_update();
    outdoor_update();
}
