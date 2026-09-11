# 任务通知代替信号量

## L1 电梯陈述

任务通知是 FreeRTOS 直接附着在某个目标任务上的轻量事件/数值通道。教程 v4 的软件定时器回调从 TimerID 取出事件位，通过 `xTaskNotify(mloop_task, event, eSetBits)` 唤醒阻塞中的 mloop；mloop 收到累积事件位后分别处理校时、Wi-Fi 或天气更新。它适合“明确唤醒一个固定任务”，不适合保存每一条都不能丢的业务数据。

## L2 架构与数据流

```text
软件定时器到期
  → Timer Service 任务执行 mloop_timer_cb()
  → 从 TimerID 取出本次事件位
  → xTaskNotify(mloop_task, event, eSetBits)
  → mloop 从通知等待中变为就绪
  → mloop 取出累积事件位，按位处理业务
```

`mloop_task` 是 mloop 任务的句柄，可理解为“准确指向该任务的引用”，不是任务函数本身。回调里的 `event` 是本次定时器到期对应的单个事件值；mloop 取到的通知值是等待期间累计的事件位。二者同名但作用域和含义不同。

## L3 机制深挖

```c
static void mloop_timer_cb(TimerHandle_t timer)
{
    uint32_t event = (uint32_t)pvTimerGetTimerID(timer);
    /* 创建软件定时器时绑定的事件位。 */

    xTaskNotify(mloop_task, event, eSetBits);
    /* 将本次事件按位 OR 到 mloop 的通知值中，并唤醒它。 */
}

static void mloop_func(void *param)
{
    while (1) {
        uint32_t events = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* 没有通知就阻塞；拿到后清空本次通知值。 */

        if (events & MLOOP_EVT_TIME_SYNC) {
            time_sync();
        }
    }
}
```

`eSetBits` 的意思是置位/合并：先到 TIME_SYNC，再到 WIFI_UPDATE，结果可同时保留两个 bit；mloop 用按位与判断分别处理。相同 bit 重复到来只仍是 1，不会记录“发生了两次”。若每一条消息、顺序或次数都不能丢，应选消息队列。

软件定时器回调运行在 Timer Service 任务，不是硬件中断。它应快速投递或通知后返回；不要在回调内长时间联网、绘制或用可能无限等待的 API 阻塞。

### 八股追问

- 待按需挂接。

## L4 权衡与替代方案

通知没有独立对象，内存和调用开销通常低，且目标任务明确；代价是它天然绑定单一接收任务，数据表达能力有限。教程中它用来合并 mloop 的周期性事件；DMA 完成这种“有/无”事件用二值信号量更直观，UI 请求这类完整业务数据使用队列更合适。
