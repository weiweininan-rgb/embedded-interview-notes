/*
 * 设备数据路径：/dev/sparam_sim 的定点二进制帧 -> 浮点 sparam_frame。
 * 它是 CSV 数据源的替代入口，转换完成后交给服务和算法的数据格式完全相同。
 */
#include "device_source.h"
#include "sparam_sim_uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int device_source_open(struct device_source *source, const char *path)
{
    /* 此处仅取得文件描述符；实际出帧由后续 ioctl START 控制。 */
    if (!source || !path) { errno = EINVAL; return -1; }
    source->fd = open(path, O_RDONLY);
    return source->fd < 0 ? -1 : 0;
}

int device_source_start(struct device_source *source)
{
    /* 显式启动生成，避免仅打开设备就开始测量。 */
    return ioctl(source->fd, SPARAM_SIM_START);
}

int device_source_next(struct device_source *source, struct sparam_frame *frame)
{
    /*
     * 一次调用等待并转换一帧。当前驱动的 read() 要求读完整结构体；
     * 仍保留 offset 循环，以保证用户态面对部分读取时不会使用残缺数据。
     */
    struct sparam_sim_frame raw;
    struct pollfd descriptor = { .fd = source->fd, .events = POLLIN };
    size_t offset = 0, i;

    /* 等驱动发布完整帧后再 read()，避免无意义轮询。 */
    if (poll(&descriptor, 1, 6000) <= 0) return -1;
    while (offset < sizeof(raw)) {
        ssize_t count = read(source->fd, (char *)&raw + offset, sizeof(raw) - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        offset += (size_t)count;
    }
    /* UAPI 约定固定 256 点；不匹配意味着驱动和用户态协议不一致。 */
    if (raw.sample_count != SPARAM_SIM_SAMPLES) { errno = EPROTO; return -1; }
    memset(frame, 0, sizeof(*frame));
    frame->frequency_hz = (double)raw.frequency_hz;
    frame->sample_count = raw.sample_count;
    /*
     * 内核样本为整数 I/Q，转换后算法只面对一种 C99 复数格式。
     * 这里不计算 S11/S21，也不改变量纲；它只做结构和类型的翻译。
     */
    for (i = 0; i < raw.sample_count; ++i) {
        frame->samples[i].incident = raw.samples[i].incident_i + I * raw.samples[i].incident_q;
        frame->samples[i].reflected = raw.samples[i].reflected_i + I * raw.samples[i].reflected_q;
        frame->samples[i].transmitted = raw.samples[i].transmitted_i + I * raw.samples[i].transmitted_q;
    }
    return 1;
}

void device_source_close(struct device_source *source)
{
    /* 关闭前先停止生成，让下一客户端从确定状态开始，并释放设备文件描述符。 */
    if (source && source->fd >= 0) {
        ioctl(source->fd, SPARAM_SIM_STOP);
        close(source->fd);
        source->fd = -1;
    }
}
