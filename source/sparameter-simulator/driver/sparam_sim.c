/*
 * 教学型 platform/字符设备驱动，周期性发布模拟 IQ。
 * 数据流：ioctl START -> delayed_work 生成 frame -> ready=true/wake_up
 *         -> 用户态 poll/read 获取完整帧 -> ready=false 等待下一帧。
 * 用于练习 read/poll/ioctl 和等待队列，不驱动真实射频硬件。
 */
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include "sparam_sim_uapi.h"

struct sparam_sim_device {
    /* 一个 DT 节点对应一套字符设备状态；本项目只实例化全局 sim。 */
    dev_t devt;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct delayed_work work; /* 周期性生成下一帧模拟数据。 */
    wait_queue_head_t wait;
    struct mutex lock;
    struct sparam_sim_frame frame;
    u32 rate_ms;
    u32 status;
    bool ready; /* 有一帧未读数据可供 read() 和 poll() 使用。 */
};

static struct sparam_sim_device sim;

static void fill_frame(struct sparam_sim_frame *frame)
{
    /*
     * 四个正交参考点循环，组成确定性的 256 点音调：
     * (30000,0) -> (0,30000) -> (-30000,0) -> (0,-30000)。
     * 这是教学用的离散正交波形，不等同于 CSV 生成器中的 7 圈/256 点音调。
     */
    static const s16 incident_i[4] = { 30000, 0, -30000, 0 };
    static const s16 incident_q[4] = { 0, 30000, 0, -30000 };
    unsigned int i;

    /* 每次发布递增序号，用户态可区分相邻两帧。 */
    frame->sequence++;
    frame->frequency_hz = 100000000ULL;
    frame->sample_count = SPARAM_SIM_SAMPLES;
    for (i = 0; i < SPARAM_SIM_SAMPLES; ++i) {
        s32 in_i = incident_i[i & 3U];
        s32 in_q = incident_q[i & 3U];
        frame->samples[i].incident_i = in_i;
        frame->samples[i].incident_q = in_q;
        /*
         * 定点近似复数乘法：reflected ≈ incident × (0.5∠30°)。
         * 443/1024≈0.433，256/1024=0.25；使用整数避免内核浮点。
         */
        frame->samples[i].reflected_i = (s16)((443 * in_i - 256 * in_q) / 1024);
        frame->samples[i].reflected_q = (s16)((256 * in_i + 443 * in_q) / 1024);
        /*
         * 定点近似：transmitted ≈ incident × (0.25∠-45°)。
         * 181/1024≈0.177；符号组合对应 -45° 的实部和虚部。
         */
        frame->samples[i].transmitted_i = (s16)((181 * in_i + 181 * in_q) / 1024);
        frame->samples[i].transmitted_q = (s16)((-181 * in_i + 181 * in_q) / 1024);
    }
}

static void generate_work(struct work_struct *work)
{
    /*
     * 延迟工作在 START 后按 rate_ms 周期执行。它先持锁生成并发布一帧，
     * 再在锁外唤醒等待者；唤醒放在锁外能缩短持锁时间。
     */
    struct sparam_sim_device *device =
        container_of(to_delayed_work(work), struct sparam_sim_device, work);

    mutex_lock(&device->lock);
    if (device->status == SPARAM_SIM_RUNNING) {
        fill_frame(&device->frame);
        device->ready = true;
        /* 仅在生成仍已开启时调度下一帧。 */
        schedule_delayed_work(&device->work, msecs_to_jiffies(device->rate_ms));
    }
    mutex_unlock(&device->lock);
    /* poll() 和阻塞 read() 都等待这一事件。 */
    wake_up_interruptible(&device->wait);
}

static ssize_t sim_read(struct file *file, char __user *buffer, size_t count,
                        loff_t *position)
{
    /*
     * 此 ABI 按帧读取；部分结构体读取会破坏 IQ 对齐，所以 count 必须刚好匹配。
     * 若设备已 STOP 且没有 ready 帧，等待条件满足后返回 0，表示无更多数据。
     */
    int status;
    (void)file; (void)position;
    if (count != sizeof(sim.frame)) return -EMSGSIZE;
    /* 新帧到达，或 STOP/remove 改变终态时都会唤醒。 */
    status = wait_event_interruptible(sim.wait,
             READ_ONCE(sim.ready) || READ_ONCE(sim.status) != SPARAM_SIM_RUNNING);
    if (status) return status;
    mutex_lock(&sim.lock);
    if (!sim.ready) { mutex_unlock(&sim.lock); return 0; }
    if (copy_to_user(buffer, &sim.frame, sizeof(sim.frame))) {
        mutex_unlock(&sim.lock); return -EFAULT;
    }
    /* 一帧只消费一次；工作队列按 rate_ms 发布下一帧。 */
    sim.ready = false;
    mutex_unlock(&sim.lock);
    return sizeof(sim.frame);
}

static unsigned int sim_poll(struct file *file, poll_table *wait)
{
    unsigned int mask = 0;
    (void)file;
    /* 先注册等待队列再检查状态，避免两步之间漏掉唤醒。 */
    poll_wait(file, &sim.wait, wait);
    /* ready 表示 read 不会阻塞；停止状态用 POLLHUP 通知用户态退出等待。 */
    if (READ_ONCE(sim.ready)) mask |= POLLIN | POLLRDNORM;
    if (READ_ONCE(sim.status) != SPARAM_SIM_RUNNING) mask |= POLLHUP;
    return mask;
}

static long sim_ioctl(struct file *file, unsigned int command, unsigned long argument)
{
    u32 value;
    (void)file;
    if (_IOC_TYPE(command) != SPARAM_SIM_IOC_MAGIC) return -ENOTTY;
    mutex_lock(&sim.lock);
    /* 所有状态转换都与 read()、延迟工作通过锁串行化。 */
    switch (command) {
    case SPARAM_SIM_START:
        /* 丢弃旧帧，保证 START 从新调度的帧开始。 */
        sim.ready = false; sim.status = SPARAM_SIM_RUNNING;
        schedule_delayed_work(&sim.work, msecs_to_jiffies(sim.rate_ms));
        break;
    case SPARAM_SIM_STOP:
        sim.status = SPARAM_SIM_STOPPED; sim.ready = false;
        cancel_delayed_work(&sim.work);
        break;
    case SPARAM_SIM_GET_STATUS:
        /* 先取锁内快照，再解锁后 copy_to_user，避免在持锁时访问用户内存。 */
        value = sim.status;
        mutex_unlock(&sim.lock);
        return copy_to_user((u32 __user *)argument, &value, sizeof(value)) ? -EFAULT : 0;
    case SPARAM_SIM_SET_RATE:
        /* 用户参数在锁外复制和校验；通过后再加锁更新下一帧的间隔。 */
        mutex_unlock(&sim.lock);
        if (copy_from_user(&value, (u32 __user *)argument, sizeof(value))) return -EFAULT;
        if (value < SPARAM_SIM_RATE_MIN_MS || value > SPARAM_SIM_RATE_MAX_MS) return -EINVAL;
        mutex_lock(&sim.lock); sim.rate_ms = value;
        break;
    default:
        mutex_unlock(&sim.lock); return -ENOTTY;
    }
    mutex_unlock(&sim.lock);
    wake_up_interruptible(&sim.wait);
    return 0;
}

static const struct file_operations sim_fops = {
    /* 通过 /dev/sparam_sim 导出的用户可见行为。 */
    .owner = THIS_MODULE,
    .read = sim_read,
    .poll = sim_poll,
    .unlocked_ioctl = sim_ioctl,
    .llseek = no_llseek,
};

static int sparam_sim_probe(struct platform_device *pdev)
{
    /* 将设备树节点绑定为一个虚拟字符设备，并初始化共享状态。 */
    int status;
    u32 rate_ms = 100;

    if (of_property_read_u32(pdev->dev.of_node, "sample-rate-ms", &rate_ms))
        dev_info(&pdev->dev, "sample-rate-ms not set, using 100 ms\n");
    if (rate_ms < SPARAM_SIM_RATE_MIN_MS || rate_ms > SPARAM_SIM_RATE_MAX_MS) {
        dev_err(&pdev->dev, "sample-rate-ms must be in range %u..%u\n",
                SPARAM_SIM_RATE_MIN_MS, SPARAM_SIM_RATE_MAX_MS);
        return -EINVAL;
    }

    mutex_init(&sim.lock); init_waitqueue_head(&sim.wait);
    INIT_DELAYED_WORK(&sim.work, generate_work);
    sim.rate_ms = rate_ms; sim.status = SPARAM_SIM_IDLE;
    /* 由内核动态分配主次设备号，无需板级静态编号。 */
    status = alloc_chrdev_region(&sim.devt, 0, 1, "sparam_sim");
    if (status) return status;
    cdev_init(&sim.cdev, &sim_fops); sim.cdev.owner = THIS_MODULE;
    status = cdev_add(&sim.cdev, sim.devt, 1);
    if (status) goto unregister;
    sim.class = class_create(THIS_MODULE, "sparam_sim");
    if (IS_ERR(sim.class)) { status = PTR_ERR(sim.class); goto delete_cdev; }
    sim.device = device_create(sim.class, &pdev->dev, sim.devt, NULL, "sparam_sim");
    if (IS_ERR(sim.device)) { status = PTR_ERR(sim.device); goto destroy_class; }
    platform_set_drvdata(pdev, &sim);
    dev_info(&pdev->dev, "registered /dev/sparam_sim, rate=%u ms\n", rate_ms);
    return 0;
destroy_class:
    class_destroy(sim.class);
delete_cdev:
    cdev_del(&sim.cdev);
unregister:
    unregister_chrdev_region(sim.devt, 1);
    return status;
}

static int sparam_sim_remove(struct platform_device *pdev)
{
    /* 销毁设备节点前，先停止发布并唤醒等待者。 */
    struct sparam_sim_device *device = platform_get_drvdata(pdev);

    mutex_lock(&device->lock);
    device->status = SPARAM_SIM_STOPPED;
    device->ready = false;
    mutex_unlock(&device->lock);
    wake_up_interruptible(&device->wait);
    cancel_delayed_work_sync(&sim.work);
    device_destroy(sim.class, sim.devt); class_destroy(sim.class);
    cdev_del(&sim.cdev); unregister_chrdev_region(sim.devt, 1);
    dev_info(&pdev->dev, "removed\n");
    return 0;
}

static const struct of_device_id sparam_sim_of_match[] = {
    /* 必须与 sparam-sim.dtsi 中的 compatible 字符串一致。 */
    { .compatible = "edu,sparam-sim" },
    { }
};
MODULE_DEVICE_TABLE(of, sparam_sim_of_match);

static struct platform_driver sparam_sim_driver = {
    /* platform 核心在匹配设备树节点出现/移除时调用 probe/remove。 */
    .probe = sparam_sim_probe,
    .remove = sparam_sim_remove,
    .driver = {
        .name = "sparam_sim",
        .of_match_table = sparam_sim_of_match,
    },
};

module_platform_driver(sparam_sim_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("IMX6ULL S-parameter simulator project");
MODULE_DESCRIPTION("Virtual fixed-point IQ source for Linux learning");
