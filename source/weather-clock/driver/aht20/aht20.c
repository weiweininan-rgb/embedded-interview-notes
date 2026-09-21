#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx.h"
#include "cpu_tick.h"

// --- 内部私有函数声明 ---
static bool aht20_write(uint8_t data[], uint32_t length);
static bool aht20_read(uint8_t data[], uint32_t length);
static bool aht20_is_ready(void);

/**
 * @brief  初始化 AHT20 传感器及其 I2C 接口
 */
bool aht20_init(void)
{
    // 1. 配置 I2C 引脚 (GPIOB 10 和 11)
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_StructInit(&GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;        // 复用功能
    // I2C 协议要求引脚必须是 开漏输出 (Open-Drain)
    GPIO_InitStruct.GPIO_OType = GPIO_OType_OD;      
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;    // 不使用内部上下拉 (I2C总线外部通常有上拉电阻)
    GPIO_InitStruct.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // 将 PB10(SCL 时钟线) 和 PB11(SDA 数据线) 映射到 I2C2
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_I2C2);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_I2C2);
    
    // 2. 配置 I2C 硬件控制器
    I2C_InitTypeDef I2C_InitStruct;
    I2C_StructInit(&I2C_InitStruct);
    I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;         // 允许发送应答信号
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStruct.I2C_ClockSpeed = 100ul * 1000ul;  // I2C 速度设为标准模式 100kHz
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStruct.I2C_OwnAddress1 = 0x00;           // 单片机自身的地址 (这里设为 0，因为单片机是主设备)
    I2C_Init(I2C2, &I2C_InitStruct);
    
    // 3. 传感器唤醒与校准序列
    cpu_delay_ms(40); // 刚上电，给传感器 40ms 时间稳定内部电路
    if (aht20_is_ready())
        return true;  // 如果传感器已经就绪，直接返回成功
    
    // 如果没就绪，发送初始化指令：0xBE (初始化命令), 0x08, 0x00
    if (!aht20_write((uint8_t[]){0xBE, 0x08, 0x00}, 3))
        return false;
    
    // 等待传感器初始化完成 (最多等 100ms)
    for (uint32_t t = 0; t < 100; t ++)
    {
        cpu_delay_ms(1);
        if (aht20_is_ready())
            return true;
    }
    
    return false; // 超时，初始化失败
}

// ==========================================
//           I2C 底层时序控制
// ==========================================

// 宏定义：带超时保护的 I2C 事件检查
// 为什么需要超时？因为如果 I2C 线断了，或者传感器坏了，单片机会死等在这个 while 循环里，导致整个系统死机。
#define I2C_CHECK_EVENT(EVENT, TIMEOUT) \
    do { \
        uint32_t timeout = TIMEOUT; \
        while (!I2C_CheckEvent(I2C2, EVENT) && timeout > 0) { \
            cpu_delay_us(10); \
            timeout -= 10; \
        } \
        if (timeout <= 0) \
            return false; /* 超时立刻退出 */ \
    } while (0)

/**
 * @brief  向 I2C 总线发送一串数据
 */
static bool aht20_write(uint8_t data[], uint32_t length)
{
    I2C_AcknowledgeConfig(I2C2, ENABLE);
    I2C_GenerateSTART(I2C2, ENABLE); // 发送起始信号 (Start)
    I2C_CHECK_EVENT(I2C_EVENT_MASTER_MODE_SELECT, 1000); // 确认成为主设备
    
    // 发送 AHT20 的 7 位 I2C 设备地址 (0x70通常是 0x38 左移一位)，并声明方向为“发送(Transmitter)”
    I2C_Send7bitAddress(I2C2, 0x70, I2C_Direction_Transmitter);
    I2C_CHECK_EVENT(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, 1000); // 确认对方应答
    
    // 循环把数组里的数据一个个发出去
    for (uint32_t i = 0; i < length; i++)
    {
        I2C_SendData(I2C2, data[i]);
        I2C_CHECK_EVENT(I2C_EVENT_MASTER_BYTE_TRANSMITTING, 1000); // 等待字节发送完成
    }
    I2C_GenerateSTOP(I2C2, ENABLE); // 发送停止信号 (Stop)，释放总线
    
    return true;
}

/**
 * @brief  从 I2C 总线读取一串数据
 */
static bool aht20_read(uint8_t data[], uint32_t length)
{
    I2C_AcknowledgeConfig(I2C2, ENABLE);
    I2C_GenerateSTART(I2C2, ENABLE);
    I2C_CHECK_EVENT(I2C_EVENT_MASTER_MODE_SELECT, 1000);
    
    // 发送 AHT20 的设备地址，并声明方向为“接收(Receiver)”
    I2C_Send7bitAddress(I2C2, 0x70, I2C_Direction_Receiver);
    I2C_CHECK_EVENT(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED, 1000);
    
    for (uint32_t i = 0; i < length; i++)
    {
        // I2C 协议规定：读最后一个字节前，必须取消应答(NACK)，告诉对方不要再发了
        if (i == length - 1)
            I2C_AcknowledgeConfig(I2C2, DISABLE);
            
        I2C_CHECK_EVENT(I2C_EVENT_MASTER_BYTE_RECEIVED, 1000); // 等待收到一个字节
        data[i] = I2C_ReceiveData(I2C2); // 把字节存进数组
    }
    I2C_GenerateSTOP(I2C2, ENABLE);
    
    return true;
}

// ==========================================
//           AHT20 业务逻辑
// ==========================================

/**
 * @brief  读取传感器内部状态寄存器
 */
static bool aht20_read_status(uint8_t *status)
{
    uint8_t cmd = 0x71; // 0x71 是 AHT20 规定的读取状态指令
    if (!aht20_write(&cmd, 1)) return false;
    if (!aht20_read(status, 1)) return false;
    return true;
}

/**
 * @brief  检查传感器是否正在测量中
 */
static bool aht20_is_busy(void)
{
    uint8_t status;
    if (!aht20_read_status(&status)) return false;
    // 状态字节的最高位(bit 7) 如果为 1 (0x80)，代表忙碌
    return (status & 0x80) != 0; 
}

/**
 * @brief  检查传感器是否已校准并准备就绪
 */
static bool aht20_is_ready(void)
{
    uint8_t status;
    if (!aht20_read_status(&status)) return false;
    // 状态字节的第 3 位(bit 3) 如果为 1 (0x08)，代表已校准
    return (status & 0x08) != 0; 
}

/**
 * @brief  触发一次温湿度测量
 */
bool aht20_start_measurement(void)
{
    // 发送触发测量命令序列：0xAC (触发测量), 0x33, 0x00
    return aht20_write((uint8_t[]){0xAC, 0x33, 0x00}, 3);
}

/**
 * @brief  死等测量完成 (通常需要几十毫秒)
 */
bool aht20_wait_for_measurement(void)
{
    for (uint32_t t = 0; t < 200; t++)
    {
        cpu_delay_ms(1); // 每次休息 1ms 再查
        if (!aht20_is_busy()) // 如果不忙了，就是测完了
        {
            return true;
        }
    }
    return false; // 超时未完成
}

/**
 * @brief  读取测量结果并将其解码为摄氏度和百分比
 * @note   这是最能体现 C 语言底层位操作(Bitwise)魅力的部分！
 */
bool aht20_read_measurement(float *temperature, float *humidity)
{
    uint8_t data[6];
    // AHT20 规定一次必须读 6 个字节：
    // data[0]: 状态字
    // data[1] ~ data[3]前半部分: 20位的湿度原始数据
    // data[3]后半部分 ~ data[5]: 20位的温度原始数据
    if (!aht20_read(data, 6))
        return false;
    
    // --- 解析湿度 (提取 20 位数据) ---
    // 1. 把 data[1] 左移 12 位，作为高 8 位
    // 2. 把 data[2] 左移 4 位，作为中间 8 位
    // 3. 把 data[3] 的高 4 位提取出来 (通过 &0xF0清空低4位)，然后右移 4 位，作为最低 4 位
    // 最后把它们按位或 (|) 拼凑成一个完整的 32 位整数
    uint32_t raw_humidity = ((uint32_t)data[1] << 12) | 
                            ((uint32_t)data[2] << 4) | 
                            ((uint32_t)(data[3] &0xF0) >> 4);
                            
    // --- 解析温度 (提取 20 位数据) ---
    // 1. 提取 data[3] 的低 4 位 (通过 &0x0F)，左移 16 位，作为最高 4 位
    // 2. 把 data[4] 左移 8 位，作为中间 8 位
    // 3. 把 data[5] 保持原样，作为最低 8 位
    uint32_t raw_temperature = ((uint32_t)(data[3] & 0x0F) << 16) | 
                               ((uint32_t)data[4] << 8) | 
                               ((uint32_t)data[5]);
    
    // --- 依据 AHT20 数据手册的官方公式，将原始二进制数值转换为人能看懂的浮点数 ---
    // 湿度公式: (Raw / 2^20) * 100% (注：0x100000 就是 2 的 20 次方)
    *humidity = (float)raw_humidity * 100.0f / (float)0x100000;
    
    // 温度公式: (Raw / 2^20) * 200 - 50 ℃
    *temperature = (float)raw_temperature * 200.0f / (float)0x100000 - 50.0f;
    
    return true;
}
