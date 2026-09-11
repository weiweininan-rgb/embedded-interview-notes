# DMA 中断中释放信号量

## L1 电梯陈述

教程 v5 用二值信号量把“DMA 已搬完数据”从 DMA 中断传给等待 LCD 传输完成的任务：任务启动 DMA 后在 `xSemaphoreTake()` 阻塞；DMA 完成中断里用 `xSemaphoreGiveFromISR()` 释放信号量，并在确实唤醒更高优先级任务时用 `portYIELD_FROM_ISR()` 请求中断退出后的尽快调度。二值信号量表示完成事件，不表示谁拥有 LCD/SPI 资源。

## L2 架构与数据流

```text
任务调用 st7789_write_gram()
  → 配置并启动 DMA：内存 → SPI 数据寄存器
  → xSemaphoreTake(write_gram_semaphore, portMAX_DELAY) 阻塞
DMA 搬运完成
  → DMA1_Stream4_IRQHandler()
  → Give 二值信号量，唤醒等待任务
  → 需要时请求 ISR 返回后立刻调度
任务恢复
  → 再确认 SPI_BSY 清除，最后结束本次 LCD 传输
```

DMA “搬完最后一个数据到 SPI 数据寄存器”并不必然表示最后一位已从 SPI 总线发送出去，所以源码在信号量返回后仍检查 SPI 的 `BSY` 标志，再结束片选。

## L3 机制深挖

```c
void DMA1_Stream4_IRQHandler(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    /* 先假定没有任务被唤醒。 */

    if (DMA_GetITStatus(DMA1_Stream4, DMA_IT_TCIF4) != RESET) {
        xSemaphoreGiveFromISR(write_gram_semaphore,
                              &higher_priority_task_woken);
        /* 中断上下文中释放“DMA 已完成”事件。 */

        portYIELD_FROM_ISR(higher_priority_task_woken);
        /* 若刚唤醒了更高优先级任务，请求中断退出后马上调度。 */

        DMA_ClearITPendingBit(DMA1_Stream4, DMA_IT_TCIF4);
        /* 确认本次 DMA 完成中断已处理，避免重复进入。 */
    }
}
```

教程源码的局部唤醒标志没有显式初始化；上面的 `pdFALSE` 初始化是更稳妥的参考写法。 `portYIELD_FROM_ISR()` 不会在 ISR 内直接执行任务，它只让调度器在 ISR 退出后有机会马上切到被唤醒的更高优先级任务；省略它通常造成额外响应延迟。

带 `FromISR` 的 API 只允许在符合工程中断优先级约束的 ISR 使用。教程 DMA 中断优先级设为 5，配置也允许它调用该类 API；更紧急的中断应只做硬件级短操作，不能随意改动 FreeRTOS 的等待关系。

### 八股追问

- 待按需挂接。

## L4 权衡与替代方案

裸机也可以用 DMA 中断 + 标志位或轮询表示完成，但等待期间常需要自行设计状态机或空转检查。这里用信号量后，等待 DMA 的任务会阻塞，CPU 可以运行别的任务。它不是 Mutex：若多个任务都直接操作 LCD/SPI，仍要用“单 UI 任务独占资源”或 Mutex 解决所有权和互斥问题。
