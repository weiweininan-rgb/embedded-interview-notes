// --- 引入必要的头文件 ---
#include <stdint.h>
#include <stdio.h>
#include "cpu_tick.h" // 引入滴答定时器，提供 cpu_delay_ms 延时功能
#include "esp_at.h"   // 引入 WiFi 模块底层驱动 (AT 指令集)
#include "page.h"     // 引入页面显示库，以便在出错时调用 error_page_display 显示黑屏报错
#include "app.h"      // 引入应用配置，这里面通常定义了 WIFI_SSID 和 WIFI_PASSWD 宏

/**
 * @brief  无线模块基础初始化
 * @note   按顺序唤醒 ESP 模块，配置为 WiFi 模式，并开启网络校时
 */
void wifi_init(void)
{
    // 1. 测试模块是否在线并重置模块
    if (!esp_at_init())
    {
        printf("[AT] init failed\n");
        goto err; // 如果失败，直接跳转到下方 err 标签处集中处理错误
    }
    printf("[AT] inited\n"); // 成功则打印日志，方便开发者在电脑串口观察
    
    // 2. 将 WiFi 模块设置为 Station 模式 (准备去连接路由器)
    if (!esp_at_wifi_init())
    {
        printf("[WIFI] init failed\n");
        goto err;
    }
    printf("[WIFI] inited\n");
    
    // 3. 开启 SNTP 网络时间同步功能 (设置东八区)
    if (!esp_at_sntp_init())
    {
        printf("[SNTP] init failed\n");
        goto err;
    }
    printf("[SNTP] inited\n");
    
    // 如果上面三步都成功了，函数安全返回
    return;
    
// --- 集中错误处理区 ---
err:
    // 调用屏幕驱动，在屏幕上显示带有黄色字体的错误信息
    error_page_display("wireless init failed");
    // 死循环卡死在这里，防止单片机在没有网络硬件的情况下继续运行业务代码引发崩溃
		
     // --- 修改后的超时处理 ---
    // 2. 删掉 while(1)，换成延时 2 秒。
    // 让错误页面在屏幕上停留 2 秒钟，让用户知道 WiFi 没连上
    cpu_delay_ms(2000); 
    // 3. 安全撤退！直接返回，放行后面的主程序。
    return; 
}


/**
 * @brief  连接 WiFi 并等待结果
 * @note   发送连接指令，然后采用“轮询”方式等待连接成功，设有 10 秒超时机制
 */
void wifi_wait_connect(void)
{
	printf("[WIFI] connecting\n");
    
    // 向 WiFi 模块发送连接指令，传入账号和密码 (密码通常定义在 app.h 中)
    esp_at_connect_wifi(WIFI_SSID, WIFI_PASSWD, NULL);
    
    // --- 核心等待机制 (轮询 Polling) ---
    // t 代表经过的时间，总共允许等待 10 * 1000 毫秒 (即 10 秒)，每次循环增加 100 毫秒
    for (uint32_t t = 0; t < 10 * 1000; t += 100)
    {
        // 每次循环先休息 100 毫秒，给 WiFi 模块一点时间去和路由器握手
        cpu_delay_ms(100);
        
        // 创建一个干净的结构体用来存放获取到的 WiFi 信息
        esp_wifi_info_t wifi = { 0 };
        
        // 发送 AT+CWSTATE? 查询当前状态。如果查询成功，且结构体里的 connected 标志位为真
        if (esp_at_get_wifi_info(&wifi) && wifi.connected)
        {
            // 成功连上啦！打印出获取到的 WiFi 名字、MAC 地址、信道和信号强度
            printf("[WIFI] Connected\n");
            printf("[WIFI] SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\n",
                wifi.ssid, wifi.bssid, wifi.channel, wifi.rssi);
            return; // 目标达成，直接退出函数，继续往下执行工程业务
        }
    }
    
//    // --- 超时处理 ---
//    // 如果 for 循环跑满了 10 秒钟还没 return，说明要么密码错了，要么没信号
//    printf("[WIFI] Connection Timeout\n"); // 在电脑串口报错
//    error_page_display("wireless connect failed"); // 在单片机彩色屏幕上报错
//    
//    // 同样进入死循环，卡死程序
//    while (1)
//    {
//        ;
//    }
		 // --- 修改后的超时处理 ---
    printf("[WIFI] Connection Timeout\n"); 
    error_page_display("wireless connect failed"); // 1. 显示错误页面
    
    // 2. 删掉 while(1)，换成延时 2 秒。
    // 让错误页面在屏幕上停留 2 秒钟，让用户知道 WiFi 没连上
    cpu_delay_ms(2000); 
    
    // 3. 安全撤退！直接返回，放行后面的主程序。
    return; 
}
