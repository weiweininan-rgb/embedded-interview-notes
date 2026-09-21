/*
 * 未来 ZynQ S 参数结果 DMAengine 客户端参考驱动。
 *
 * 完整展示客户端的软件职责：probe/open/ioctl、结果槽位、prep/submit/
 * issue_pending、callback、poll/read、STOPPING/terminate_sync 和 remove。
 *
 * 这不是当前 IMX6ULL 驱动，也未在 ZynQ/AXI DMA/PL 上构建或运行。设备树
 * dmas 选择器、AXI DMA 参数、PL 格式、超时时间、错误上报方式和目标内核
 * 仍须未来硬件确认；代码不包含物理地址、IRQ 或吞吐数字。
 */
#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "sparam_result_uapi_draft.h"

/* 教学预设：四个结果槽位共享一条 S2MM 接收通道；不代表最终容量。 */
#define SPARAM_DMA_SLOT_COUNT 4
#define SPARAM_DMA_DEVICE_NAME "sparam-result-ref"

enum sparam_dma_slot_state {
    SPARAM_DMA_SLOT_FREE,      /* CPU 和 DMA 都不占用，可提交。 */
    SPARAM_DMA_SLOT_QUEUED,    /* 描述符已提交，DMA 可以写入。 */
    SPARAM_DMA_SLOT_STOPPING,  /* 正在同步终止，禁止读取和复用。 */
    SPARAM_DMA_SLOT_READY,     /* DMA 已完成，等待用户态读取。 */
    SPARAM_DMA_SLOT_READING,   /* 已被一个 read() 独占。 */
};

struct sparam_dma_client;
struct sparam_dma_slot;

/* 一个描述符的 callback 身份卡；其生命周期与槽位一致。 */
struct sparam_dma_transfer {
    struct sparam_dma_slot *slot;
    u64 sequence;
    dma_cookie_t cookie;
};

/* 一个槽位对应一块可容纳一帧结果的 DMA 一致性 DDR 缓冲区。 */
struct sparam_dma_slot {
    struct sparam_dma_client *client;
    void *cpu_addr;             /* CPU/read() 使用。 */
    dma_addr_t dma_addr;        /* DMA 使用；不假设等于物理地址。 */
    size_t length;
    u64 sequence;               /* 客户端内部提交编号。 */
    dma_cookie_t cookie;        /* DMAengine 本次提交编号。 */
    enum sparam_dma_slot_state state;
    struct sparam_dma_transfer transfer;
};

/* 一个匹配到的 platform device 对应一个客户端实例和一条接收通道。 */
struct sparam_dma_client {
    struct device *dev;
    struct device *dma_dev;     /* 真正执行 DMA 的控制器设备。 */
    struct dma_chan *result_rx;  /* DTS dma-names = "result-rx"。 */
    struct miscdevice miscdev;

    /* 可睡眠控制操作串行化：ARM、STOP、read 收尾和 remove。 */
    struct mutex control_lock;
    /* callback 与进程上下文共享的槽位状态，只在极短临界区持有。 */
    spinlock_t state_lock;
    wait_queue_head_t result_wait;
    wait_queue_head_t close_wait;
    atomic_t open_count;

    struct sparam_dma_slot slots[SPARAM_DMA_SLOT_COUNT];
    enum sparam_result_driver_state_v1 driver_state;
    bool armed;
    bool removing;

    u64 next_sequence;
    u64 complete_count;
    u64 stale_callback_count;
    u64 dma_error_count;
    u64 timeout_count;
    u64 no_slot_count;
    u64 read_fault_count;
    u64 pl_overrun_count;
    unsigned int ready_slots;
    unsigned int queued_slots;
};

/* callback 只发布状态并唤醒，不做 copy_to_user()、校准或网络处理。 */
static void sparam_dma_complete(void *argument)
{
    struct sparam_dma_transfer *transfer = argument;
    struct sparam_dma_slot *slot = transfer->slot;
    struct sparam_dma_client *client = slot->client;
    unsigned long flags;

    spin_lock_irqsave(&client->state_lock, flags);
    if (slot->state == SPARAM_DMA_SLOT_QUEUED &&
        slot->sequence == transfer->sequence &&
        slot->cookie == transfer->cookie) {
        slot->state = SPARAM_DMA_SLOT_READY;
        client->queued_slots--;
        client->ready_slots++;
        client->complete_count++;
    } else {
        /* STOPPING 后的迟到通知或重复通知不能发布成新结果。 */
        client->stale_callback_count++;
    }
    spin_unlock_irqrestore(&client->state_lock, flags);
    wake_up_interruptible(&client->result_wait);
}

/* prep/submit 尚未启动传输时失败，可以安全恢复为 FREE。 */
static void sparam_dma_rollback_unissued(struct sparam_dma_slot *slot)
{
    struct sparam_dma_client *client = slot->client;
    unsigned long flags;

    spin_lock_irqsave(&client->state_lock, flags);
    if (slot->state == SPARAM_DMA_SLOT_QUEUED) {
        slot->state = SPARAM_DMA_SLOT_FREE;
        client->queued_slots--;
    }
    client->dma_error_count++;
    spin_unlock_irqrestore(&client->state_lock, flags);
}

/*
 * 调用者持有 control_lock。本函数只 prep/submit 一个 FREE 槽位；真正启动由
 * 调用者批量执行 dma_async_issue_pending()，与官方调用链一致。
 */
static int sparam_dma_submit_slot(struct sparam_dma_client *client,
                                  struct sparam_dma_slot *slot)
{
    struct dma_async_tx_descriptor *desc;
    dma_cookie_t cookie;
    unsigned long flags;

    spin_lock_irqsave(&client->state_lock, flags);
    if (slot->state != SPARAM_DMA_SLOT_FREE) {
        spin_unlock_irqrestore(&client->state_lock, flags);
        return -EBUSY;
    }
    slot->state = SPARAM_DMA_SLOT_QUEUED;
    slot->sequence = ++client->next_sequence;
    slot->cookie = 0;
    slot->transfer.slot = slot;
    slot->transfer.sequence = slot->sequence;
    slot->transfer.cookie = 0;
    client->queued_slots++;
    spin_unlock_irqrestore(&client->state_lock, flags);

    /* 槽位已被本次提交独占为 QUEUED，此后清零不会擦掉未读 READY 帧。 */
    memset(slot->cpu_addr, 0, slot->length);

    /* 一帧占一块连续 DMA 缓冲区；AXI DMA provider 可在内部组织硬件 SG 链。 */
    desc = dmaengine_prep_slave_single(client->result_rx,
                                       slot->dma_addr,
                                       slot->length,
                                       DMA_DEV_TO_MEM,
                                       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
    if (!desc) {
        sparam_dma_rollback_unissued(slot);
        return -EIO;
    }

    desc->callback = sparam_dma_complete;
    desc->callback_param = &slot->transfer;
    cookie = dmaengine_submit(desc);
    if (dma_submit_error(cookie)) {
        sparam_dma_rollback_unissued(slot);
        return cookie;
    }

    /* issue_pending() 未调用，callback 不会早于这里看见未写完的 cookie。 */
    spin_lock_irqsave(&client->state_lock, flags);
    slot->cookie = cookie;
    slot->transfer.cookie = cookie;
    spin_unlock_irqrestore(&client->state_lock, flags);
    return 0;
}

/* ARM_RX：把当前所有 FREE 槽位预提交到同一 result-rx 通道。 */
static int sparam_dma_arm_rx(struct sparam_dma_client *client)
{
    unsigned long flags;
    unsigned int index;
    unsigned int submitted = 0;
    int first_error = 0;
    int ret;

    mutex_lock(&client->control_lock);
    spin_lock_irqsave(&client->state_lock, flags);
    if (client->removing) {
        spin_unlock_irqrestore(&client->state_lock, flags);
        ret = -ENODEV;
        goto out_unlock;
    }
    if (client->driver_state == SPARAM_RESULT_STATE_STOPPING) {
        spin_unlock_irqrestore(&client->state_lock, flags);
        ret = -EBUSY;
        goto out_unlock;
    }
    if (client->driver_state == SPARAM_RESULT_STATE_ERROR) {
        spin_unlock_irqrestore(&client->state_lock, flags);
        ret = -EIO;
        goto out_unlock;
    }
    client->armed = true;
    client->driver_state = SPARAM_RESULT_STATE_ARMED;
    spin_unlock_irqrestore(&client->state_lock, flags);
    for (index = 0; index < SPARAM_DMA_SLOT_COUNT; index++) {
        ret = sparam_dma_submit_slot(client, &client->slots[index]);
        if (ret == -EBUSY)
            continue;
        if (ret) {
            if (!first_error)
                first_error = ret;
            continue;
        }
        submitted++;
    }

    if (submitted)
        dma_async_issue_pending(client->result_rx);

    /* 已有 QUEUED/READY 槽位时，重复 ARM_RX 是幂等操作。 */
    if (!submitted && first_error) {
        spin_lock_irqsave(&client->state_lock, flags);
        client->driver_state = SPARAM_RESULT_STATE_ERROR;
        spin_unlock_irqrestore(&client->state_lock, flags);
        ret = first_error;
    } else {
        if (!submitted) {
            spin_lock_irqsave(&client->state_lock, flags);
            client->no_slot_count++;
            spin_unlock_irqrestore(&client->state_lock, flags);
        }
        ret = 0;
    }

out_unlock:
    mutex_unlock(&client->control_lock);
    return ret;
}

/*
 * STOP_RX 与未来超时钩子共用此路径。terminate_sync() 只能在可睡眠上下文
 * 调用；成功返回才证明传输和旧 callback 都已静止，随后才能复用缓冲区。
 */
static int sparam_dma_stop_channel(struct sparam_dma_client *client,
                                   bool timed_out)
{
    unsigned long flags;
    unsigned int index;
    int ret;

    mutex_lock(&client->control_lock);
    spin_lock_irqsave(&client->state_lock, flags);
    client->armed = false;
    client->driver_state = SPARAM_RESULT_STATE_STOPPING;
    if (timed_out)
        client->timeout_count++;
    for (index = 0; index < SPARAM_DMA_SLOT_COUNT; index++) {
        if (client->slots[index].state == SPARAM_DMA_SLOT_QUEUED)
            client->slots[index].state = SPARAM_DMA_SLOT_STOPPING;
    }
    spin_unlock_irqrestore(&client->state_lock, flags);

    ret = dmaengine_terminate_sync(client->result_rx);

    spin_lock_irqsave(&client->state_lock, flags);
    if (ret) {
        /* 终止失败时保留 STOPPING，不能冒险释放仍可能被写的内存。 */
        client->dma_error_count++;
        client->driver_state = SPARAM_RESULT_STATE_ERROR;
    } else {
        for (index = 0; index < SPARAM_DMA_SLOT_COUNT; index++) {
            if (client->slots[index].state == SPARAM_DMA_SLOT_STOPPING)
                client->slots[index].state = SPARAM_DMA_SLOT_FREE;
        }
        client->queued_slots = 0;
        client->driver_state = SPARAM_RESULT_STATE_IDLE;
    }
    spin_unlock_irqrestore(&client->state_lock, flags);

    mutex_unlock(&client->control_lock);
    wake_up_interruptible(&client->result_wait);
    return ret;
}

/* 在 state_lock 内把最早完成的一帧 READY 独占为 READING。 */
static struct sparam_dma_slot *sparam_dma_claim_ready(struct sparam_dma_client *client)
{
    struct sparam_dma_slot *chosen = NULL;
    unsigned long flags;
    unsigned int index;

    spin_lock_irqsave(&client->state_lock, flags);
    for (index = 0; index < SPARAM_DMA_SLOT_COUNT; index++) {
        struct sparam_dma_slot *slot = &client->slots[index];

        if (slot->state != SPARAM_DMA_SLOT_READY)
            continue;
        if (!chosen || slot->sequence < chosen->sequence)
            chosen = slot;
    }
    if (chosen) {
        chosen->state = SPARAM_DMA_SLOT_READING;
        client->ready_slots--;
    }
    spin_unlock_irqrestore(&client->state_lock, flags);
    return chosen;
}

/* copy_to_user() 后收尾；成功且仍 ARMED 时为连续采集补回描述符。 */
static void sparam_dma_finish_read(struct sparam_dma_client *client,
                                  struct sparam_dma_slot *slot,
                                  bool copied)
{
    struct sparam_result_frame_v1 *frame = slot->cpu_addr;
    unsigned long flags;
    bool resubmit = false;

    mutex_lock(&client->control_lock);
    spin_lock_irqsave(&client->state_lock, flags);
    if (slot->state == SPARAM_DMA_SLOT_READING) {
        if (copied) {
            if (frame->header.magic == SPARAM_RESULT_FRAME_MAGIC &&
                (frame->header.status & SPARAM_RESULT_STATUS_PL_OVERFLOW))
                client->pl_overrun_count++;
            slot->state = SPARAM_DMA_SLOT_FREE;
            resubmit = client->armed && !client->removing;
        } else {
            slot->state = SPARAM_DMA_SLOT_READY;
            client->ready_slots++;
            client->read_fault_count++;
        }
    }
    spin_unlock_irqrestore(&client->state_lock, flags);

    if (resubmit && !sparam_dma_submit_slot(client, slot))
        dma_async_issue_pending(client->result_rx);
    mutex_unlock(&client->control_lock);

    if (!copied)
        wake_up_interruptible(&client->result_wait);
}

/*
 * miscdevice 的 open 入口：从 miscdevice 找回 client，并保存到 file。
 * open_count 让 remove() 等到已打开的文件全部关闭后再释放缓冲区。
 */
static int sparam_dma_open(struct inode *inode, struct file *file)
{
    struct miscdevice *misc = file->private_data;
    struct sparam_dma_client *client =
        container_of(misc, struct sparam_dma_client, miscdev);
    unsigned long flags;
    int ret = 0;

    (void)inode;
    spin_lock_irqsave(&client->state_lock, flags);
    if (client->removing) {
        ret = -ENODEV;
    } else {
        atomic_inc(&client->open_count);
        file->private_data = client;
    }
    spin_unlock_irqrestore(&client->state_lock, flags);
    return ret;
}

/* close() 对应入口：只减少引用计数，不在这里单独释放 devm 内存。 */
static int sparam_dma_release(struct inode *inode, struct file *file)
{
    struct sparam_dma_client *client = file->private_data;

    (void)inode;
    if (atomic_dec_and_test(&client->open_count))
        wake_up(&client->close_wait);
    return 0;
}

/*
 * 领取一个 READY 槽位，在锁外复制整帧到用户态；阻塞语义由等待队列实现。
 * 复制成功后槽位可重新提交，失败则恢复 READY，避免悄悄丢掉该帧。
 */
static ssize_t sparam_dma_read(struct file *file, char __user *buffer,
                               size_t count, loff_t *position)
{
    struct sparam_dma_client *client = file->private_data;
    struct sparam_dma_slot *slot;
    int ret;

    (void)position;
    if (count != sizeof(struct sparam_result_frame_v1))
        return -EMSGSIZE;

    for (;;) {
        slot = sparam_dma_claim_ready(client);
        if (slot)
            break;
        if (READ_ONCE(client->removing))
            return -ENODEV;
        if (READ_ONCE(client->driver_state) == SPARAM_RESULT_STATE_ERROR)
            return -EIO;
        if (!READ_ONCE(client->armed) &&
            READ_ONCE(client->driver_state) == SPARAM_RESULT_STATE_IDLE)
            return 0;
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        ret = wait_event_interruptible(client->result_wait,
                 READ_ONCE(client->ready_slots) > 0 ||
                 READ_ONCE(client->removing) ||
                 READ_ONCE(client->driver_state) == SPARAM_RESULT_STATE_ERROR ||
                 (!READ_ONCE(client->armed) &&
                  READ_ONCE(client->driver_state) == SPARAM_RESULT_STATE_IDLE));
        if (ret)
            return ret;
    }

    /* 用户页访问可能睡眠；READING 保证槽位仍由本次 read() 独占。 */
    if (copy_to_user(buffer, slot->cpu_addr, slot->length)) {
        sparam_dma_finish_read(client, slot, false);
        return -EFAULT;
    }

    sparam_dma_finish_read(client, slot, true);
    return slot->length;
}

/* 把 READY、错误和停止状态转换为用户态 poll() 可观察的事件。 */
static unsigned int sparam_dma_poll(struct file *file, poll_table *wait)
{
    struct sparam_dma_client *client = file->private_data;
    unsigned long flags;
    unsigned int mask = 0;

    poll_wait(file, &client->result_wait, wait);
    spin_lock_irqsave(&client->state_lock, flags);
    if (client->ready_slots)
        mask |= POLLIN | POLLRDNORM;
    if (client->driver_state == SPARAM_RESULT_STATE_ERROR)
        mask |= POLLERR;
    if (client->removing ||
        (!client->armed && client->driver_state == SPARAM_RESULT_STATE_IDLE))
        mask |= POLLHUP;
    spin_unlock_irqrestore(&client->state_lock, flags);
    return mask;
}

/* 在同一把状态锁下制作一致的计数器快照，供 GET_STATUS ioctl 返回。 */
static void sparam_dma_snapshot_status(struct sparam_dma_client *client,
                                       struct sparam_result_status_v1 *status)
{
    unsigned long flags;

    memset(status, 0, sizeof(*status));
    spin_lock_irqsave(&client->state_lock, flags);
    status->state = client->driver_state;
    status->ready_slots = client->ready_slots;
    status->queued_slots = client->queued_slots;
    status->next_sequence = client->next_sequence;
    status->complete_count = client->complete_count;
    status->stale_callback_count = client->stale_callback_count;
    status->dma_error_count = client->dma_error_count;
    status->timeout_count = client->timeout_count;
    status->no_slot_count = client->no_slot_count;
    status->read_fault_count = client->read_fault_count;
    status->pl_overrun_count = client->pl_overrun_count;
    spin_unlock_irqrestore(&client->state_lock, flags);
}

/* 字符设备控制入口：启动接收、同步停止或查询可观察状态。 */
static long sparam_dma_ioctl(struct file *file, unsigned int command,
                             unsigned long argument)
{
    struct sparam_dma_client *client = file->private_data;
    struct sparam_result_status_v1 status;

    if (_IOC_TYPE(command) != SPARAM_RESULT_IOC_MAGIC)
        return -ENOTTY;

    switch (command) {
    case SPARAM_RESULT_IOC_ARM_RX:
        return sparam_dma_arm_rx(client);
    case SPARAM_RESULT_IOC_STOP_RX:
        return sparam_dma_stop_channel(client, false);
    case SPARAM_RESULT_IOC_GET_STATUS:
        sparam_dma_snapshot_status(client, &status);
        return copy_to_user((void __user *)argument, &status, sizeof(status)) ?
               -EFAULT : 0;
    default:
        return -ENOTTY;
    }
}

static const struct file_operations sparam_dma_fops = {
    .owner = THIS_MODULE,
    .open = sparam_dma_open,
    .release = sparam_dma_release,
    .read = sparam_dma_read,
    .poll = sparam_dma_poll,
    .unlocked_ioctl = sparam_dma_ioctl,
    .llseek = no_llseek,
};

/* 只释放已经成功分配的 DMA 一致性缓冲区，用于 probe 失败和 remove。 */
static void sparam_dma_free_buffers(struct sparam_dma_client *client,
                                    unsigned int allocated)
{
    unsigned int index;

    for (index = 0; index < allocated; index++) {
        struct sparam_dma_slot *slot = &client->slots[index];

        dma_free_coherent(client->dma_dev, slot->length,
                          slot->cpu_addr, slot->dma_addr);
        slot->cpu_addr = NULL;
    }
}

/*
 * DTS 匹配后的初始化入口：创建 client、取得已有通道、分配槽位并注册字符设备。
 * 它不创建 AXI DMA 控制器，也不填写物理地址或 IRQ。
 */
static int sparam_dma_probe(struct platform_device *pdev)
{
    struct sparam_dma_client *client;
    unsigned int index;
    int ret;

    client = devm_kzalloc(&pdev->dev, sizeof(*client), GFP_KERNEL);
    if (!client)
        return -ENOMEM;

    client->dev = &pdev->dev;
    mutex_init(&client->control_lock);
    spin_lock_init(&client->state_lock);
    init_waitqueue_head(&client->result_wait);
    init_waitqueue_head(&client->close_wait);
    atomic_set(&client->open_count, 0);
    client->driver_state = SPARAM_RESULT_STATE_IDLE;

    /* "result-rx" 由未来客户端 DTS 的 dma-names 定义；不创建 AXI DMA。 */
    client->result_rx = dma_request_chan(&pdev->dev, "result-rx");
    if (IS_ERR(client->result_rx))
        return PTR_ERR(client->result_rx);
    client->dma_dev = client->result_rx->device->dev;

    for (index = 0; index < SPARAM_DMA_SLOT_COUNT; index++) {
        struct sparam_dma_slot *slot = &client->slots[index];

        slot->client = client;
        slot->length = sizeof(struct sparam_result_frame_v1);
        slot->state = SPARAM_DMA_SLOT_FREE;
        slot->transfer.slot = slot;
        slot->cpu_addr = dma_alloc_coherent(client->dma_dev,
                                             slot->length,
                                             &slot->dma_addr,
                                             GFP_KERNEL);
        if (!slot->cpu_addr) {
            ret = -ENOMEM;
            goto free_buffers;
        }
    }

    client->miscdev.minor = MISC_DYNAMIC_MINOR;
    client->miscdev.name = SPARAM_DMA_DEVICE_NAME;
    client->miscdev.fops = &sparam_dma_fops;
    client->miscdev.parent = &pdev->dev;
    ret = misc_register(&client->miscdev);
    if (ret)
        goto free_buffers;

    platform_set_drvdata(pdev, client);
    dev_info(&pdev->dev, "reference DMA result client registered as /dev/%s\n",
             SPARAM_DMA_DEVICE_NAME);
    return 0;

free_buffers:
    sparam_dma_free_buffers(client, index);
    dma_release_channel(client->result_rx);
    return ret;
}

/*
 * 卸载入口：先禁止新 open/read，再等待文件关闭并同步终止 DMA，最后释放资源。
 * 若无法确认 DMA 已停止，则宁可保留缓冲区，也不能释放仍可能被写入的内存。
 */
static int sparam_dma_remove(struct platform_device *pdev)
{
    struct sparam_dma_client *client = platform_get_drvdata(pdev);
    unsigned long flags;
    int ret;

    spin_lock_irqsave(&client->state_lock, flags);
    client->removing = true;
    spin_unlock_irqrestore(&client->state_lock, flags);
    misc_deregister(&client->miscdev);
    wake_up_interruptible(&client->result_wait);
    wait_event(client->close_wait, atomic_read(&client->open_count) == 0);

    ret = sparam_dma_stop_channel(client, false);
    if (ret) {
        /* 终止失败时不能释放仍可能被 DMA 写入的缓冲区或通道。 */
        dev_err(&pdev->dev,
                "DMA termination failed; buffers were not released safely\n");
        return ret;
    }

    sparam_dma_free_buffers(client, SPARAM_DMA_SLOT_COUNT);
    dma_release_channel(client->result_rx);
    return 0;
}

static const struct of_device_id sparam_dma_of_match[] = {
    /* 项目参考 compatible；真实 DTS 仍需未来评审，当前没有 DTB 成功证据。 */
    { .compatible = "edu,sparam-result-dma-client" },
    { }
};
MODULE_DEVICE_TABLE(of, sparam_dma_of_match);

static struct platform_driver sparam_dma_driver = {
    .probe = sparam_dma_probe,
    .remove = sparam_dma_remove,
    .driver = {
        .name = "sparam-dma-client-ref",
        .of_match_table = sparam_dma_of_match,
    },
};
module_platform_driver(sparam_dma_driver);

MODULE_DESCRIPTION("Reference-only S-parameter DMAengine result client");
MODULE_LICENSE("GPL");
