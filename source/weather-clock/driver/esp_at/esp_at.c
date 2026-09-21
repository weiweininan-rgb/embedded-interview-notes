// --- 1. 引入标准 C 库和工程自定义头文件 ---
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "stm32f4xx.h"  // STM32 底层寄存器库
#include "cpu_tick.h"   // 滴答定时器延时库
#include "esp_at.h"     // 本驱动的头文件，存放结构体和函数声明

// --- 2. 宏定义 ---
#define ESP_AT_DEBUG    1 // 调试开关：设为 1 时，会在串口打印所有发出去的命令和收到的回复，方便查错

// 这是一个计算数组元素个数的万能公式：数组总字节数 ÷ 单个元素的字节数
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// --- 3. 定义 AT 指令的响应状态 ---
typedef enum
{
    AT_ACK_NONE,  // 没有收到有效回复
    AT_ACK_OK,    // 收到 "OK" (执行成功)
    AT_ACK_ERROR, // 收到 "ERROR" (执行失败)
    AT_ACK_BUSY,  // 收到 "busy p…" (模块正忙)
    AT_ACK_READY, // 收到 "ready" (模块重启完毕准备就绪)
} at_ack_t;

// 定义一个结构体，用来把“状态枚举”和“具体的英文字符串”绑定在一起
typedef struct
{
    at_ack_t ack;
    const char *string;
} at_ack_match_t;

// 建立一个匹配字典，告诉程序：如果收到右边的字符串，就返回左边的状态
static const at_ack_match_t at_ack_matches[] = 
{
    {AT_ACK_OK, "OK\r\n"},
    {AT_ACK_ERROR, "ERROR\r\n"},
    {AT_ACK_BUSY, "busy p…\r\n"},
    {AT_ACK_READY, "ready\r\n"},
};

// --- 4. 关键全局变量 ---
// 接收缓冲区：单片机用来存放从 WiFi 模块收到的所有数据的“大水缸”，最大 1024 字节
static char rxbuf[1024];

// --- 5. 内部私有函数声明 (提前预告) ---
static void esp_at_usart_write(const char *data);
static bool esp_at_wait_boot(uint32_t timeout);

// ==========================================
//           底层硬件接口部分
// ==========================================

/**
 * @brief  初始化连接 WiFi 模块的串口 (USART2)
 */
static void esp_at_usart_init(void)
{
    USART_InitTypeDef USART_InitStructure;
    USART_StructInit(&USART_InitStructure);

    // 配置串口参数：波特率 115200 (和 WiFi 模块出厂默认一致)，8位数据位，1位停止位，无校验
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    
    // 把单片机的引脚 PA2 (TX) 和 PA3 (RX) 映射到串口 2 的功能上
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);
    
    // 配置 PA2 和 PA3 为复用功能 (AF) 高速模式
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 正式启动串口 2
    USART_Init(USART2, &USART_InitStructure);
    USART_Cmd(USART2, ENABLE);
}

/**
 * @brief  向 WiFi 模块发送字符串指令 (带回车换行)
 */
static void esp_at_usart_write(const char *data)
{
    // 只要字符串还没结束，就一个字一个字地往外发
    while (data && *data)
    {
        // 死等，直到发射舱(TXE)空了，再把下一个字符塞进去
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
        USART_SendData(USART2, *data++);
    }
    // AT 指令的规矩：每条指令发完，必须紧跟一个回车符 '\r' 和换行符 '\n'，否则 WiFi 模块不认
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, '\r');
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
    USART_SendData(USART2, '\n');
}

// ==========================================
//           AT 指令核心收发逻辑
// ==========================================

/**
 * @brief  查字典：对比收到的字符串，看它属于哪种状态
 */
static at_ack_t match_internal_ack(const char *str)
{
    // 遍历我们前面定义的字典 at_ack_matches
    for (uint32_t i = 0; i < ARRAY_SIZE(at_ack_matches); i++)
    {
        // 如果收到的字符串和字典里的完全一样，就返回对应的状态枚举 (比如 AT_ACK_OK)
        if (strcmp(str, at_ack_matches[i].string) == 0)
            return at_ack_matches[i].ack;
    }
    
    return AT_ACK_NONE; // 没查到，返回 NONE
}

/**
 * @brief  死等 WiFi 模块的回复 (带超时保护)
 * @note   这是最核心的接收函数！它负责把串口收到的乱码拼成句子，并判断是不是 OK。
 */
static at_ack_t esp_at_usart_wait_receive(uint32_t timeout)
{
    uint32_t rxlen = 0;           // 当前收到了多少个字符
    const char *line = rxbuf;     // 指向当前正在处理的这一行字
    uint64_t start = cpu_get_ms(); // 记录开始等待的时间戳，用于超时判断
    
    rxbuf[0] = '\0'; // 清空水缸
    
    // 只要水缸还没装满，就一直收
    while (rxlen < sizeof(rxbuf) - 1)
    {
        // 死等串口收到新数据 (RXNE = 接收寄存器非空)
        while (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == RESET)
        {
            // 如果等太久(超过了设定的 timeout 毫秒)，就放弃，返回 NONE
            if (cpu_get_ms() - start >= timeout)
                return AT_ACK_NONE;
        }
        
        // 拿到一个新字符，放进水缸，并且给末尾封上 '\0' 结束符
        rxbuf[rxlen++] = USART_ReceiveData(USART2);
        rxbuf[rxlen] = '\0';
        
        // 如果刚收到的这个字符是换行符 '\n'，说明 WiFi 模块说完了一句话！
        if (rxbuf[rxlen - 1] == '\n')
        {
            // 把这刚说完的一句话拿去查字典
            at_ack_t ack = match_internal_ack(line);
            // 如果查到了是有用的状态 (比如 OK 或者 ERROR)，就立刻返回，不再收了
            if (ack != AT_ACK_NONE)
                return ack;
                
            // 如果没查到有用状态，说明这只是中间过程的数据，把指针往下移，准备收下一句
            line = rxbuf + rxlen;
        }
    }
    
    return AT_ACK_NONE;
}

/**
 * @brief  发送 AT 指令并等待 OK 确认
 * @param  command: 要发送的字符串指令 (比如 "AT+CWMODE=1")
 * @param  timeout: 最多等多少毫秒
 */
bool esp_at_write_command(const char *command, uint32_t timeout)
{
#if ESP_AT_DEBUG
    printf("[DEBUG] Send: %s\n", command); // 开启调试时，在这里打印我们发了啥
#endif

    esp_at_usart_write(command); // 真正发出去
    at_ack_t ack = esp_at_usart_wait_receive(timeout); // 开始等待回复

#if ESP_AT_DEBUG
    printf("[DEBUG] Response:\n%s\n", rxbuf); // 开启调试时，在这里打印我们收到了啥
#endif

    return ack == AT_ACK_OK; // 只有当模块回复了 "OK\r\n"，才算发送成功，返回 true
}

/**
 * @brief  获取上次收到的完整回复数据
 */
const char *esp_at_get_response(void)
{
    return rxbuf;
}

// ==========================================
//           WiFi 模块业务初始化与连接
// ==========================================

/**
 * @brief  不断发送基础 "AT" 指令，试图唤醒模块，直到它回 OK 为止
 */
static bool esp_at_wait_boot(uint32_t timeout)
{
    for (int t = 0; t < timeout; t += 100)
    {
        if (esp_at_write_command("AT", 100))
            return true;
    }
    return false;
}

/**
 * @brief  等待模块重启后发出 "ready" 信号
 */
bool esp_at_wait_ready(uint32_t timeout)
{
    return esp_at_usart_wait_receive(timeout) == AT_ACK_READY;
}

/**
 * @brief  总初始化流程
 */
bool esp_at_init(void)
{
    esp_at_usart_init(); // 初始化单片机串口
    
    if (!esp_at_wait_boot(3000))                 // 等待模块醒来
        return false;
    if (!esp_at_write_command("AT+RESTORE", 2000)) // 强制模块恢复出厂设置 (清空以前乱七八糟的配置)
        return false;
    if (!esp_at_wait_ready(5000))                // 等待模块恢复设置并重启完成
        return false;
    
    return true;
}

/**
 * @brief  设置为 Station 模式 (像手机一样去连接别人的 WiFi)
 */
bool esp_at_wifi_init(void)
{
    return esp_at_write_command("AT+CWMODE=1", 2000); 
}

/**
 * @brief  连接指定账号密码的 WiFi
 */
bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac)
{
    if (ssid == NULL || pwd == NULL)
        return false; // 没给账号密码，直接拒绝干活
    
    char cmd[128];
    // 按照 AT 指令的语法格式，把账号密码拼接成：AT+CWJAP="账号","密码"
    int len = snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    
    if (mac) // 如果还指定了路由器的高级 MAC 地址绑定，继续拼在后面
        snprintf(cmd + len, sizeof(cmd) - len, ",\"%s\"", mac);
    
    return esp_at_write_command(cmd, 5000); // 发送指令，给 5 秒钟时间让它连
}

// ==========================================
//           WiFi 状态与参数解析
// ==========================================

/**
 * @brief  解析模块返回的连接状态字符串
 */
static bool parse_cwstate_response(const char *response, esp_wifi_info_t *info)
{
    // 假设收到的 response 是这样：
    // AT+CWSTATE?
    // +CWSTATE:2,"Xiaomi Mi MIX 3_5577"
    // OK

	response = strstr(response, "+CWSTATE:"); // 用 strstr 函数直接跳到有用的数据开头处
	if (response == NULL)
		return false;
	
	int wifi_state;
    // 使用 sscanf 进行魔法切割：
    // %d 提取数字状态(存给wifi_state)
    // %63[^\"] 提取两个双引号之间的任意字符(即使有空格)，最多63个字，存进 info->ssid
	if (sscanf(response, "+CWSTATE:%d,\"%63[^\"]", &wifi_state, info->ssid) != 2)
		return false;
	
	info->connected = (wifi_state == 2); // AT手册规定：状态等于 2 代表已经连上 IP 地址了
	
	return true;
}

/**
 * @brief  解析模块返回的详细 WiFi 信号参数
 */
static bool parse_cwjap_response(const char *response, esp_wifi_info_t *info)
{
    // 假设收到的 response 是这样：
    // AT+CWJAP?
    // +CWJAP:"Xiaomi Mi MIX 3_5577","da:b5:3a:e3:2f:60",9,-48,0,1,3,0,1
    // OK
	
	response = strstr(response, "+CWJAP:");
	if (response == NULL)
		return false;
	
    // 同样的 sscanf 魔法：连续提取名字、MAC地址(%17[^\"])、信道(%d)、信号强度(%d)
	if (sscanf(response, "+CWJAP:\"%63[^\"]\",\"%17[^\"]\",%d,%d", info->ssid, info->bssid, &info->channel, &info->rssi) != 4)
		return false;
	
	return true;
}

/**
 * @brief  组合技：查询当前 WiFi 是否连接，如果连上了就顺便把信号强度等信息读出来
 */
bool esp_at_get_wifi_info(esp_wifi_info_t *info)
{
    if (!esp_at_write_command("AT+CWSTATE?", 2000)) return false;
    if (!parse_cwstate_response(esp_at_get_response(), info)) return false;
    
    // 如果上一步解析发现没连上(比如密码错了)，那下面查详细信息肯定查不到，就直接不查了
    if (info->connected == true)
    {
        if (!esp_at_write_command("AT+CWJAP?", 2000)) return false;
        if (!parse_cwjap_response(esp_at_get_response(), info)) return false;
    }
    
    return true;
}

/**
 * @brief  简单版组合技：只关心到底连没连上，不管详细信号强度
 */
bool wifi_is_connected(void)
{
    esp_wifi_info_t info;
    if (esp_at_get_wifi_info(&info))
    {
        return info.connected;
    }
    return false;
}

// ==========================================
//           SNTP 网络对时与 HTTP 天气解析
// ==========================================

/**
 * @brief  开启 SNTP 对时服务 (配置为东 8 区北京时间)
 */
bool esp_at_sntp_init(void)
{
    if (!esp_at_write_command("AT+CIPSNTPCFG=1,8", 2000))
        return false;
    return true;
}

/**
 * @brief  辅助工具：把英文字母的月份翻译成数字 (如 "Jan" -> 1)
 */
static uint8_t month_str_to_num(const char *month_str)
{
	const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	for (uint8_t i = 0; i < 12; i++)
	{
		if (strcmp(month_str, months[i]) == 0) return i + 1;
	}
	return 0;
}

/**
 * @brief  辅助工具：把英文字母的星期翻译成数字 (如 "Mon" -> 1)
 */
static uint8_t weekday_str_to_num(const char *weekday_str)
{
	const char *weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
	for (uint8_t i = 0; i < 7; i++) {
		if (strcmp(weekday_str, weekdays[i]) == 0) return i + 1;
	}
	return 0;
}

/**
 * @brief  解析模块返回的乱糟糟的网络时间字符串
 */
static bool parse_cipsntptime_response(const char *response, esp_date_time_t *date)
{
//	AT+CIPSNTPTIME?
//	+CIPSNTPTIME:Sun Jul 27 14:07:19 2025
//	OK
	char weekday_str[8];
	char month_str[4];
	
	response = strstr(response, "+CIPSNTPTIME:");
    // 使用 sscanf 按照 "星期 月份 日 时:分:秒 年" 的特定格式一一抓取对应数字
    // %hhu 代表专门提取 8位无符号整数 (如 uint8_t)
	if (sscanf(response, "+CIPSNTPTIME:%3s %3s %hhu %hhu:%hhu:%hhu %hu", 
			   weekday_str, month_str, 
			   &date->day, &date->hour, &date->minute, &date->second, &date->year) != 7)
		return false;
	
    // 翻译刚刚抓下来的英文字母
	date->weekday = weekday_str_to_num(weekday_str);
	date->month = month_str_to_num(month_str);
	
	return true;
}

/**
 * @brief  向模块索要当前时间
 */
bool esp_at_sntp_get_time(esp_date_time_t *date)
{
    if (!esp_at_write_command("AT+CIPSNTPTIME?", 2000)) return false;
    if (!parse_cipsntptime_response(esp_at_get_response(), date)) return false;
    return true;
}

/**
 * @brief  向网络发送 HTTP 请求 (核心：抓取知心天气的 JSON 数据)
 * @param  url: 包含了你的城市和 API 秘钥的长网址
 * @return 返回抓取到的整个包含 JSON 的庞大字符串的首地址
 */
const char *esp_at_http_get(const char *url)
{
// 模块最终发出去的指令长这样：
// AT+HTTPCLIENT=2,1,"https://api.seniverse.com/v3/weather/now.json?key=XXX...",,,2

    char *txbuf = rxbuf; // 为了省内存，借用接收水缸 rxbuf 来临时装配我们要发送的指令！
    
    // 把那个长长的 URL 组装进 AT+HTTPCLIENT 框架里
    snprintf(txbuf, sizeof(rxbuf), "AT+HTTPCLIENT=2,1,\"%s\",,,2", url);
    
    // 发出去，并给它 5 秒钟的时间去网上拉取数据
    bool ret = esp_at_write_command(txbuf, 5000);
    
    // 如果成功抓取(ret==true)，就把存满了整个 JSON 天气数据的水缸首地址交还给上层解析
    return ret ? esp_at_get_response() : NULL;
}
