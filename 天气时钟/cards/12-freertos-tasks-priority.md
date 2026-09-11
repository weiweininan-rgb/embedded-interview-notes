# FreeRTOS 任务划分与优先级

## L1 电梯陈述

教程 v5 的 main() 先用 `xTaskCreate()` 登记 init 任务，再用 `vTaskStartScheduler()` 启动调度器；创建任务本身不会立即执行任务函数。init 完成板级、Wi-Fi 和业务初始化后用 `vTaskDelete(NULL)` 删除自身。之后任务是否运行由“优先级 + 当前状态”决定：就绪态中最高优先级任务先运行，等待队列、通知、信号量或延时的任务处于阻塞态，不占 CPU。

## L2 架构与数据流

### v5 启动链

```c
int main(void)
{
    board_lowlevel_init();
    /* 时钟、芯片等裸机基础初始化。 */

    xTaskCreate(main_init, "init", 1024, NULL, 9, NULL);
    /* 登记任务入口、名字、1024 个栈元素、参数、优先级和可选句柄。 */

    vTaskStartScheduler();
    /* 创建内核所需任务并开始选择就绪任务运行。 */

    while (1) { ; }
    /* 正常启动成功后不会执行到这里。 */
}
```

`main_init()` 依次完成板级初始化、欢迎页、Wi-Fi 初始化/连接、主页和 `main_loop_init()`，最后调用 `vTaskDelete(NULL)`；`NULL` 的意思是删除“当前正在运行的 init 任务”。任务名 `"init"` 主要用于调试和观察，不是调度依据。若后续需要从其他任务操作某任务，创建时应保存任务句柄；教程用 `mloop_task` 保存 mloop 任务的句柄，以便向它发送通知。

### 状态与优先级

```text
创建成功后 → 就绪态 → 被调度运行
运行中调用 vTaskDelay / xQueueReceive / xSemaphoreTake 等等待 API
           → 阻塞态
等待条件满足或延时到期 → 就绪态
被调度器选中         → 再次运行
vTaskDelete(NULL)    → 不再运行，等待 Idle 清理动态内存
```

教程配置 `configMAX_PRIORITIES = 10`，可用任务优先级为 0～9；数值越大，优先级越高。init 是 9，UI 任务是 8，mloop/workqueue 是 5，Idle 是 0。高优先级不应忙等：若优先级 9 的任务持续就绪，优先级 8 的 UI 任务就没有运行机会。相同优先级任务才涉及时间片轮转；本工程开启抢占与时间片，但不能把调度简单说成“主要靠轮询时间片”。

## L3 机制深挖

### 任务栈、FreeRTOS 堆与删除

- `xTaskCreate()` 从 FreeRTOS 堆为 TCB（任务控制块）和该任务的独立栈申请内存。任务、队列、信号量、软件定时器等动态对象也共享这块堆。
- 教程配置启用动态分配，堆大小为 `92 * 1024`，使用 `heap_4.c`。这不是 MCU 的全部 RAM。
- `configSTACK_DEPTH_TYPE` 是 `uint32_t`，因此 `1024` 是栈元素数，约为 `1024 * 4 = 4096` 字节，不应误称为 1024 字节。
- 栈要覆盖调用链、局部变量/数组、格式化函数等的峰值。 `uxTaskGetStackHighWaterMark()` 返回历史最低剩余栈空间，单位是栈元素；本工程可近似乘 4 换算字节。应在最深业务路径跑完后查看，余量过低就增栈或降低峰值占用。
- `configCHECK_FOR_STACK_OVERFLOW` 是兜底检测，不等于栈大小一定合适。教程没有本轮实测的高水位数据。

`vTaskStartScheduler()` 自动创建 Idle 任务。Idle 是优先级 0 的普通任务，不是调度器；它只在没有其他任务就绪时运行，并会清理由动态创建后删除的任务所遗留的 TCB 和栈。

### 常用返回值与超时

- `BaseType_t` 是 FreeRTOS 常用结果类型。 `xTaskCreate()` 成功返回 `pdPASS`，失败返回 `pdFAIL`；失败常见原因是 FreeRTOS 堆不足。
- `xQueueCreate()` 返回队列句柄；返回 `NULL` 表示创建失败。
- `pdTRUE`/`pdFALSE` 常用于“是否得到目标”，例如 `xSemaphoreTake()` 在超时内得到信号量返回 `pdTRUE`。
- `portMAX_DELAY` 不是函数，是“可一直等待”的超时时间参数。它让任务在条件不满足时阻塞而非空转；但不适合可能阻塞的定时器回调。

### 相对延时与固定周期

- `vTaskDelay(pdMS_TO_TICKS(100))` 从“本次调用时刻”开始延时 100 ms；教程 Wi-Fi 连接轮询用它，每次检查后让出 CPU。
- `vTaskDelayUntil()` 以预定节拍为基准，适合希望尽量保持固定周期的任务。前者会累加本次工作耗时，后者用于减少这种漂移。
- 教程的 mloop/v7 周期工作主要通过软件定时器到期后通知或投递工作来组织，而不是让一个大循环手动管理所有周期。

### 已确认学习要点：FreeRTOS 堆与任务栈

- 教程 v5 的 `third_lib/freertos/portable/FreeRTOSConfig.h` 配置了动态分配，`configTOTAL_HEAP_SIZE` 为 `92 * 1024`，并使用 `heap_4.c`。任务、队列、信号量和软件定时器等动态对象从 FreeRTOS 堆申请内存；这块堆不等同于整颗 STM32 的全部 RAM。
- `xTaskCreate()` 动态创建任务时会为任务控制块（TCB）和该任务的独立栈分配内存。本工程的 `configSTACK_DEPTH_TYPE` 是 `uint32_t`，所以 `xTaskCreate(..., 1024, ...)` 的栈大小约为 `1024 * 4 = 4096` 字节。
- 栈大小应覆盖函数调用链、局部变量、局部数组和格式化函数等的峰值消耗，不能只按业务数据量估算。`uxTaskGetStackHighWaterMark()` 返回历史最低栈余量，单位是栈元素；本工程中可乘以 4 换算为近似字节数。
- 高水位应在任务跑过最深工作路径后再观察，例如 `mloop` 完成校时、天气解析和 UI 更新后，或 UI 任务完成字符串/图片绘制与 DMA 等待后。高水位过低时增加栈或减少峰值占用；余量长期明显过大时，仅在充分测试后再考虑缩小栈。
- 工程已启用 `configCHECK_FOR_STACK_OVERFLOW`，但仍应结合高水位观测评估栈余量。

### 已确认学习要点：Idle 任务

- `vTaskStartScheduler()` 会自动创建优先级 0 的 Idle 任务；它是普通任务，不是调度器。优先级最低是为了只在没有其他就绪任务时才使用 CPU。
- 教程采用动态分配。`init` 等动态创建的任务调用 `vTaskDelete(NULL)` 后不再运行，其 TCB 和栈由 Idle 任务后续清理并归还 FreeRTOS 堆。因此若高优先级任务长期不阻塞，Idle 无法运行，删除任务的内存清理也会被推迟。

### 八股追问

- 待按需挂接。

## L4 权衡与替代方案

可以先用保守栈大小并通过高水位检查逐步收敛；它比只凭直觉分配更可靠。若创建任务、队列或信号量失败，应沿返回值处理，而不是继续使用空句柄。教程源码中多处用 `portMAX_DELAY` 简化正常链路；产品代码还应明确队列满、DMA 完成信号迟迟不到等超时后的策略。
