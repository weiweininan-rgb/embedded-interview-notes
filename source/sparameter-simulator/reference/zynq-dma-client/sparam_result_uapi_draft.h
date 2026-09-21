/*
 * 未来 ZynQ S 参数“结果块”字符设备 UAPI 草案。
 *
 * 这是独立的教学参考合同，不能替代 include/sparam_sim_uapi.h：后者是当前 IMX6ULL
 * 虚拟 IQ 字符设备接口。本文件不表示 PL 位宽、字节序、DMA 长度或硬件已经确认。
 * 草案未发布给真实硬件，因此本次可在 ABI v1 中补齐算法分支；一旦真实发布，结构变化必须提升 ABI 版本。
 */
#ifndef SPARAM_RESULT_UAPI_DRAFT_H
#define SPARAM_RESULT_UAPI_DRAFT_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef __s32 sparam_result_s32;
typedef __s64 sparam_result_s64;
typedef __u16 sparam_result_u16;
typedef __u32 sparam_result_u32;
typedef __u64 sparam_result_u64;
#else
#include <stdint.h>
typedef int32_t sparam_result_s32;
typedef int64_t sparam_result_s64;
typedef uint16_t sparam_result_u16;
typedef uint32_t sparam_result_u32;
typedef uint64_t sparam_result_u64;
#if !defined(_WIN32)
#include <sys/ioctl.h>
#endif
#endif

/*
 * MinGW 没有 Linux 的 sys/ioctl.h；以下仅让本草案的主机布局测试可计算 ioctl 编号。
 * Linux 内核路径使用 linux/ioctl.h，Linux 用户态路径使用 sys/ioctl.h，均会优先提供官方定义。
 */
#ifndef _IO
#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS 2
#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT (_IOC_SIZESHIFT + _IOC_SIZEBITS)
#define _IOC_NONE 0U
#define _IOC_READ 2U
#define _IOC(dir, type, nr, size) \
    (((dir) << _IOC_DIRSHIFT) | ((type) << _IOC_TYPESHIFT) | \
     ((nr) << _IOC_NRSHIFT) | ((size) << _IOC_SIZESHIFT))
#define _IO(type, nr) _IOC(_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, data_type) _IOC(_IOC_READ, (type), (nr), sizeof(data_type))
#endif

/* read() 返回的帧标识；这是本草案的 ABI 自检值，不是 PL 寄存器值。 */
#define SPARAM_RESULT_FRAME_MAGIC 0x53505231U /* ASCII: "SPR1" */
#define SPARAM_RESULT_ABI_VERSION 1U

/* 所有复数累计/频谱数值均以有符号整数实部和虚部承载。 */
struct sparam_result_complex_accum_v1 {
    sparam_result_s64 real;
    sparam_result_s64 imag;
};

/* status 是 PL 已经写入结果块的有效性标志；DMA 自身超时/错误不伪装成一帧有效结果。 */
#define SPARAM_RESULT_STATUS_VALID          (1U << 0)
#define SPARAM_RESULT_STATUS_PL_OVERFLOW    (1U << 1)
#define SPARAM_RESULT_STATUS_PL_MATH_ERROR  (1U << 2)
#define SPARAM_RESULT_STATUS_PL_FORMAT_ERROR (1U << 3)

/* 结果来源算法。DMA 客户端只搬运此字段和负载，不执行相关法或 FFT。 */
enum sparam_result_algorithm_v1 {
    SPARAM_RESULT_ALGORITHM_CORRELATION = 1,
    SPARAM_RESULT_ALGORITHM_FFT_BIN = 2,
};

/*
 * 所有结果帧共有的头部。frequency_hz 始终是本次结果的实际测量频率：
 * 对 FFT 路线，它是 PL 根据本振和 bin 位置换算后的绝对频率，而非仅仅的 bin 序号。
 */
struct sparam_result_frame_header_v1 {
    sparam_result_u32 magic;
    sparam_result_u16 abi_version;
    sparam_result_u16 header_bytes;
    sparam_result_u32 frame_bytes;
    sparam_result_u32 algorithm;

    sparam_result_u64 sequence;      /* 项目级频点结果编号。 */
    sparam_result_u64 frequency_hz;  /* 对应本次累计的扫频频点。 */
    sparam_result_u32 sample_count;  /* PL 实际参与本次累计的样本数。 */
    sparam_result_u32 status;        /* SPARAM_RESULT_STATUS_*。 */
    sparam_result_u32 error_flags;   /* PL 定义的更细错误位；当前草案不分配具体含义。 */
    sparam_result_u32 reserved0;
};

/*
 * 相关法负载：Paa 是入射波自相关/功率累计量；Pba/Pta 是反射/传输与入射的复相关累计量。
 * 三者使用同一个 2 的幂缩放指数。例如 -20 表示 raw 整数单位代表 2^-20 个相关累计单位。
 */
struct sparam_result_correlation_payload_v1 {
    sparam_result_s32 corr_scale_exp2;
    sparam_result_u32 reserved0;
    sparam_result_u64 paa_accum;
    struct sparam_result_complex_accum_v1 pba_accum;
    struct sparam_result_complex_accum_v1 pta_accum;
};

/* FFT 窗函数：不同窗函数的幅度归一化与频谱泄漏特性不同，必须随结果携带。 */
enum sparam_result_fft_window_v1 {
    SPARAM_RESULT_FFT_WINDOW_RECTANGULAR = 0,
    SPARAM_RESULT_FFT_WINDOW_HANN = 1,
    SPARAM_RESULT_FFT_WINDOW_HAMMING = 2,
    SPARAM_RESULT_FFT_WINDOW_BLACKMAN = 3,
};

/*
 * FFT-bin 负载：A[k]/B[k]/T[k] 是第 bin_index 个复数频谱值。
 * fft_size 是一次 FFT 输入的时域样本数；bin_index 是该结果位于 0..fft_size-1 的哪一格；
 * window 说明做 FFT 前对时域样本采用的窗函数；average_count 是已累计的 FFT 块数。
 * spectrum_scale_exp2 是频谱整数的二进制缩放指数。以上均为教学预设的元数据，不是实机配置证据。
 */
struct sparam_result_fft_bin_payload_v1 {
    sparam_result_u32 fft_size;
    sparam_result_u32 bin_index;
    sparam_result_u32 window;
    sparam_result_u32 average_count;
    sparam_result_s32 spectrum_scale_exp2;
    sparam_result_u32 reserved0;
    struct sparam_result_complex_accum_v1 incident_bin;
    struct sparam_result_complex_accum_v1 reflected_bin;
    struct sparam_result_complex_accum_v1 transmitted_bin;
};

/*
 * 一次 read() 的固定大小容器。为保持字符设备 read() 简单，它总是返回 sizeof(frame) 字节；
 * frame_bytes 表示 header 加“当前算法负载”的有效字节数，未使用的 union 尾部必须清零。
 * 它是本机字符设备接口，不是 TCP 报文；目标 ARM 内核/用户态构建时必须重新执行布局测试。
 */
struct sparam_result_frame_v1 {
    struct sparam_result_frame_header_v1 header;
    union {
        struct sparam_result_correlation_payload_v1 correlation;
        struct sparam_result_fft_bin_payload_v1 fft_bin;
    } payload;
};

/* ARM 只准备/提交 DMA 接收槽位，不直接声明已启动 AD9361 或 PL。 */
enum sparam_result_driver_state_v1 {
    SPARAM_RESULT_STATE_IDLE = 0,
    SPARAM_RESULT_STATE_ARMED = 1,
    SPARAM_RESULT_STATE_STOPPING = 2,
    SPARAM_RESULT_STATE_ERROR = 3,
};

/*
 * GET_STATUS 的内核状态快照；不是 PL 结果帧。no_slot_count 表示客户端没有可提交槽位的次数，
 * read_fault_count 表示 copy_to_user() 失败后把帧恢复 READY 的次数；pl_overrun_count 来自结果帧
 * 的 PL_OVERFLOW 状态。三者原因不同，不能合并成一个笼统错误数。
 */
struct sparam_result_status_v1 {
    sparam_result_u32 state;
    sparam_result_u32 ready_slots;
    sparam_result_u32 queued_slots;
    sparam_result_u32 reserved0;
    sparam_result_u64 next_sequence;
    sparam_result_u64 complete_count;
    sparam_result_u64 stale_callback_count;
    sparam_result_u64 dma_error_count;
    sparam_result_u64 timeout_count;
    sparam_result_u64 no_slot_count;
    sparam_result_u64 read_fault_count;
    sparam_result_u64 pl_overrun_count;
};

/*
 * 预设字符设备行为：
 *   ARM_RX   ：准备多个 DMA 接收槽位；实际 PL 启动仍走未来已确认的控制路径。
 *   STOP_RX  ：停止新提交，并对在途槽位执行安全终止/回收。
 *   GET_STATUS：读取以上状态快照。
 * read() 固定读取一个完整 sparam_result_frame_v1；poll() 在有 READY 帧时返回 POLLIN。
 */
#define SPARAM_RESULT_IOC_MAGIC 'R'
#define SPARAM_RESULT_IOC_ARM_RX _IO(SPARAM_RESULT_IOC_MAGIC, 1)
#define SPARAM_RESULT_IOC_STOP_RX _IO(SPARAM_RESULT_IOC_MAGIC, 2)
#define SPARAM_RESULT_IOC_GET_STATUS \
    _IOR(SPARAM_RESULT_IOC_MAGIC, 3, struct sparam_result_status_v1)

#endif
