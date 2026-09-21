#ifndef SPARAM_H
#define SPARAM_H

/* 用户态各数据源和算法共用的核心数据合同。 */
#include <complex.h>
#include <stddef.h>
#include <stdint.h>

/* 统一帧最多容纳 1024 点；少于 2 点无法形成有意义的帧内估计。 */
#define SPARAM_MAX_SAMPLES 1024U
#define SPARAM_MIN_SAMPLES 2U

enum sparam_algorithm {
    /* 相关法在时域按入射波共轭对齐；FFT 在入射最强频点相除。 */
    SPARAM_ALG_CORRELATION = 0,
    SPARAM_ALG_FFT = 1,
};

enum sparam_state {
    SPARAM_IDLE = 0,
    SPARAM_RUNNING = 1,
    SPARAM_DONE = 2,
    SPARAM_ERROR = 3,
};

struct sparam_sample {
    /*
     * 同一时刻同步采集的三路复数样本。
     * 后续算法只关心 reflected/incident 和 transmitted/incident，
     * 因而三路必须属于同一次采样，不能来自不同时间点。
     */
    double complex incident;
    double complex reflected;
    double complex transmitted;
};

struct sparam_frame {
    /*
     * 一个频点对应一帧，帧内所有样本频率相同。
     * CSV 的相邻同频行或设备的一次完整 read() 都要转换成此结构；
     * 算法层因此不需要知道原始数据来自文件还是字符设备。
     */
    double frequency_hz;
    size_t sample_count;
    struct sparam_sample samples[SPARAM_MAX_SAMPLES];
};

struct sparam_result {
    /*
     * 单帧可展示结果。幅度转换为 dB 方便比较，仍必须保存相位；
     * 仅保存 dB 会丢失校准和后续复数运算所需的信息。
     */
    double frequency_hz;
    enum sparam_algorithm algorithm;
    /* 保留复数真值，扫频校准不能只依赖已经拆开的 dB 和相位。 */
    double complex s11;
    double complex s21;
    double s11_mag_db;
    double s11_phase_deg;
    double s21_mag_db;
    double s21_phase_deg;
    uint64_t elapsed_us;
    int status;
};

const char *sparam_algorithm_name(enum sparam_algorithm algorithm);
int sparam_parse_algorithm(const char *text, enum sparam_algorithm *algorithm);
/*
 * 计算一帧的 S11/S21。
 * 输入：统一 IQ 帧和算法选择；输出：填充 result，返回 0。
 * 失败时返回 -1 并设置 errno，result->status 同样记录本次计算状态。
 */
int sparam_calculate(const struct sparam_frame *frame,
                     enum sparam_algorithm algorithm,
                     struct sparam_result *result);

#endif
