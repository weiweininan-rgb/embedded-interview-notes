# 消息队列单线程 UI 渲染

## L1 电梯陈述

教程 v6 用 xQueueCreate() 创建 UI 消息队列，生产者用 xQueueSend() 投递显示请求，UI 任务用 xQueueReceive() 阻塞等待并成为 LCD 的唯一写者。队列适合传递有类型、有参数的完整业务消息；它会复制消息结构体，但不会复制结构体中指针所指的数据，必须明确字符串等动态数据的所有权和释放时机。

## L2 架构与数据流

```text
业务任务调用 ui_write_string(str)
  → 为 str 申请一份堆内存 pstr 并复制字符
  → 构造 ui_message_t：动作 + 坐标/样式 + pstr
  → xQueueSend(ui_queue, &msg, ...)
  → UI 任务 xQueueReceive(ui_queue, &msg, ...)
  → 按动作绘制，再 vPortFree(pstr)
```

队列长度是 16，消息大小是 sizeof(ui_message_t)。队列空时，xQueueReceive(..., portMAX_DELAY) 让 UI 任务阻塞；队列满时，xQueueSend() 是否等待、等待多久取决于第三个超时参数。

## L3 机制深挖

发送方局部 msg、队列内部 msg 副本和接收方局部 msg 是三份结构体；pstr 的“地址数值”被复制了，但它指向的字符串内容不会自动复制。若把发送方局部数组的地址放进消息，函数返回后地址可能失效，UI 晚些使用会导致乱码或异常。教程在 UI 字符串路径上先用 pvPortMalloc() 复制文本，再由 UI 用 vPortFree() 释放，因此跨任务生命周期明确。

### 与通知、信号量和 Mutex 的选择

| 传递目标 | 适合机制 | 教程例子 |
| --- | --- | --- |
| 固定任务要处理哪些合并事件 | 任务通知 | v4 mloop 的 TimerID 事件位 |
| 一个“完成/可用”事件 | 二值信号量 | v5 DMA 完成中断唤醒等待任务 |
| 完整数据/动作，顺序和参数有意义 | 消息队列 | v6 UI 消息 |
| 共享资源的使用权 | Mutex | v4～v7 应用/驱动源码未创建；UI 单写者架构避免了多写者争抢 |

Mutex 具有资源所有权和优先级继承语义；二值信号量没有，因此不能把“DMA 完成信号”当作 LCD 多任务互斥锁。

### v7 自定义 workqueue 的边界

v7 的 workqueue 不是 FreeRTOS 原生对象，而是“队列 + 一个 worker 任务”的自定义封装。队列消息保存函数指针 work 和参数指针 param，worker 收到后执行 msg.work(msg.param)。函数地址如 time_sync 在程序运行期间有效；但若 param 指向发送者的局部数组，队列仍只复制地址，数据依旧会失效。

v7 存在两类软件定时器回调：app_timer_cb() 直接调用 time_update()，而 work_timer_cb() 把校时、Wi-Fi、天气等工作投递给 worker。因此不能笼统说“v7 所有定时器回调都只投递工作”。另外 workqueue_run() 使用 xQueueSend(..., portMAX_DELAY)；若队列满，Timer Service 任务可能在投递时阻塞。改进时可用不等待投递，并按业务决定丢弃、合并或重试；这是代码审查建议，不是实测现象。

### 八股追问

- 待按需挂接。

## L4 权衡与替代方案

队列的优势是数据边界清楚、生产者与 UI 解耦；代价是内存占用和满队列策略都要设计。UI 单写者比“每次写 LCD 都加锁”更容易维护；若确实需要多个任务直接访问同一资源，再考虑 Mutex，而不是滥用队列或二值信号量。
