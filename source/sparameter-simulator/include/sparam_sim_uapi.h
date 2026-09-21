#ifndef SPARAM_SIM_UAPI_H
#define SPARAM_SIM_UAPI_H

/* 虚拟 IQ 字符设备的内核/用户态 ABI，字段宽度不得随意改变。 */
#include <linux/ioctl.h>
#include <linux/types.h>

#define SPARAM_SIM_SAMPLES 256U
#define SPARAM_SIM_RATE_MIN_MS 10U
#define SPARAM_SIM_RATE_MAX_MS 5000U

enum sparam_sim_status {
    SPARAM_SIM_IDLE = 0,
    SPARAM_SIM_RUNNING = 1,
    SPARAM_SIM_STOPPED = 2,
    SPARAM_SIM_ERROR = 3,
};

struct sparam_sim_iq {
    /* 有符号定点 I/Q 分量，用户态转换为 double complex。 */
    __s16 incident_i;
    __s16 incident_q;
    __s16 reflected_i;
    __s16 reflected_q;
    __s16 transmitted_i;
    __s16 transmitted_q;
};

struct sparam_sim_frame {
    /* 一个完整 read() 单元，调用者必须请求恰好等于此结构体的大小。 */
    __u64 sequence;
    __u64 frequency_hz;
    __u32 sample_count;
    __u32 reserved;
    struct sparam_sim_iq samples[SPARAM_SIM_SAMPLES];
};

/* 这些命令控制模拟帧生成，不控制真实射频前端。 */
#define SPARAM_SIM_IOC_MAGIC 'S'
#define SPARAM_SIM_START _IO(SPARAM_SIM_IOC_MAGIC, 1)
#define SPARAM_SIM_STOP _IO(SPARAM_SIM_IOC_MAGIC, 2)
#define SPARAM_SIM_GET_STATUS _IOR(SPARAM_SIM_IOC_MAGIC, 3, __u32)
#define SPARAM_SIM_SET_RATE _IOW(SPARAM_SIM_IOC_MAGIC, 4, __u32)

#endif
