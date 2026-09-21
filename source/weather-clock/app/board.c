// --- 1. 引入必要的头文件 ---
#include <stdint.h>     // 引入标准整数类型
#include <stdio.h>      // 引入标准输入输出库 (为了使用 printf)
#include "stm32f4xx.h"  // 引入 STM32F4 官方标准固件库（包含了所有底层寄存器的定义）

// --- 2. 引入各个外设的自定义驱动头文件 ---
#include "cpu_tick.h"   // 滴答定时器驱动 (负责毫秒级延时)
#include "console.h"    // 控制台驱动 (可能封装了串口初始化等)
#include "rtc.h"        // 实时时钟驱动 (负责系统走时)
#include "aht20.h"      // 室内温湿度传感器驱动
#include "st7789.h"     // 彩色液晶屏幕驱动

/**
 * @brief  底层硬件初始化函数
 * @note   核心作用是打开各个外设的“时钟” (相当于给硬件模块通电)
 */
void board_lowlevel_init(void)
{
    // --- 开启 GPIO (通用引脚) 端口的时钟 ---
    // AHB1 是一条高速总线，这里把引脚 A、B、C、D、E 的供电全部打开，因为后面的外设全都要用到它们
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
    
    // --- 开启通信接口的时钟 ---
    // USART1 (串口1) 挂载在超高速的 APB2 总线上，用于连接电脑打印调试信息
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    
    // USART2 (串口2，连WiFi)、I2C2 (连温湿度传感器)、SPI2 (连屏幕) 挂载在 APB1 总线上
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    
    // --- RTC (硬件时钟) 特殊配置区 ---
    // 1. 开启电源控制(PWR)模块的时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    // 2. 允许访问后备寄存器 (RTC 的数据存在一个特殊的保险箱里，断电靠纽扣电池维持，默认是锁死的，必须先解锁)
    PWR_BackupAccessCmd(ENABLE);
    // 3. 开启外部低速振荡器 (LSE，通常是一个 32.768kHz 的晶振，专门给时钟提供精准心跳)
    RCC_LSEConfig(RCC_LSE_ON);
    // 4. 死等，直到 LSE 晶振稳定起振为止
    while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);
    // 5. 将 RTC 的时钟源切换为刚才起振的 LSE 晶振
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
}

/**
 * @brief  板级综合初始化函数
 * @note   核心作用是调用各个模块的 init 函数，让它们正式开始工作
 */
void board_init(void)
{
    // 1. 启动滴答定时器 (提供延时功能)
    cpu_tick_init();
    
    // 2. 初始化控制台 (通常里面会调用 USART1 的参数配置)
    console_init();
    
    // 3. 打印系统编译时间！
    // __DATE__ 和 __TIME__ 是 C 语言编译器的魔法宏，在每次点击“Rebuild”时，
    // 编译器会自动把电脑当前的日期和时间变成字符串填在这里。
    printf("[SYS] Build Date: %s %s\n", __DATE__, __TIME__);
    
    // 4. 依次初始化 硬件时钟、温湿度传感器、液晶屏幕
    rtc_init();
    aht20_init();
    st7789_init();
}

/**
 * @brief  C语言底层打印字符重定向函数
 * @note   这是极其重要的一段代码！它把标准的 printf 强行绑定到了单片机的串口1上。
 */
int fputc(int ch, FILE *f)
{
    // 将 printf 想打印的一个字符 (ch)，强转为 8 位数据，塞进 USART1 (串口1) 的发射舱
    USART_SendData(USART1, (uint8_t)ch);
    
    // 死等串口 1 的状态标志位 TXE (Transmit Data Register Empty：发送数据寄存器为空)
    // 只有当上一个字符飞出去了，发射舱空了，循环才会结束，允许程序继续往下走
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    
    // 返回成功打印的字符，这是 C 语言标准库的要求
    return ch;
}
