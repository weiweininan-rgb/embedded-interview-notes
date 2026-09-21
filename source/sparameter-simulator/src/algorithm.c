#define _POSIX_C_SOURCE 200809L
/*
 * 算法层只接收统一 sparam_frame，不关心数据源来自 CSV 还是字符设备。
 * 两个实现都输出同一组复数比 S11/S21，再由 fill_result 转为 dB 和相位。
 */
#include "sparam.h"

#include <errno.h>
#include <math.h>
#include <strings.h>
#include <time.h>

#define SPARAM_EPSILON 1e-12
#define PI 3.14159265358979323846

static uint64_t elapsed_us(const struct timespec *start, const struct timespec *end)
{
    uint64_t seconds = (uint64_t)(end->tv_sec - start->tv_sec);
    int64_t nanoseconds = end->tv_nsec - start->tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    return seconds * 1000000ULL + (uint64_t)nanoseconds / 1000ULL;
}

static void fill_result(double complex s11, double complex s21,
                        struct sparam_result *result)
{
    /*
     * 输入仍是复数比，例如 0.5∠30°；输出拆为界面/CSV 直接使用的字段。
     * 幅度：20log10(|S|)；相位：carg(S) 的弧度再换成角度。
     */
    double s11_magnitude = cabs(s11);
    double s21_magnitude = cabs(s21);

    result->s11 = s11;
    result->s21 = s21;
    result->s11_mag_db = s11_magnitude > SPARAM_EPSILON
                           ? 20.0 * log10(s11_magnitude) : -INFINITY;
    result->s21_mag_db = s21_magnitude > SPARAM_EPSILON
                           ? 20.0 * log10(s21_magnitude) : -INFINITY;
    result->s11_phase_deg = carg(s11) * 180.0 / PI;
    result->s21_phase_deg = carg(s21) * 180.0 / PI;
}

static int correlation_calculate(const struct sparam_frame *frame,
                                 struct sparam_result *result)
{
    /*
     * 以入射波 A 为参考做相关：
     *   S11 = Σ(B_i × conj(A_i)) / Σ(A_i × conj(A_i))
     *   S21 = Σ(T_i × conj(A_i)) / Σ(A_i × conj(A_i))
     * conj(A_i) 消除入射波自身旋转，保留反射/传输相对入射的固定变化。
     */
    double complex incident_power = 0.0;
    double complex reflected_cross = 0.0;
    double complex transmitted_cross = 0.0;
    size_t i;

    for (i = 0; i < frame->sample_count; ++i) {
        /* 对当前样本 i 生成 A_i*，三路累加必须使用同一个参考。 */
        double complex reference = conj(frame->samples[i].incident);
        incident_power += frame->samples[i].incident * reference;
        reflected_cross += frame->samples[i].reflected * reference;
        transmitted_cross += frame->samples[i].transmitted * reference;
    }
    if (cabs(incident_power) < SPARAM_EPSILON) {
        errno = ERANGE;
        return -1;
    }
    fill_result(reflected_cross / incident_power,
                transmitted_cross / incident_power, result);
    return 0;
}

static int is_power_of_two(size_t value)
{
    return value && !(value & (value - 1U));
}

static void fft(double complex *values, size_t count)
{
    /*
     * 原地 radix-2 Cooley-Tukey：先位倒序，再逐级蝶形运算。
     * 调用后 values 不再是时域 IQ，而是各候选旋转频率的复数频谱，
     * 因此 fft_calculate 先复制 frame 内容，避免破坏原始输入帧。
     */
    size_t i, j, length;

    for (i = 1, j = 0; i < count; ++i) {
        size_t bit = count >> 1U;
        for (; j & bit; bit >>= 1U)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double complex temporary = values[i];
            values[i] = values[j];
            values[j] = temporary;
        }
    }

    for (length = 2; length <= count; length <<= 1U) {
        double angle = -2.0 * PI / (double)length;
        double complex root = cos(angle) + I * sin(angle);
        size_t start;
        for (start = 0; start < count; start += length) {
            double complex weight = 1.0;
            for (j = 0; j < length / 2U; ++j) {
                double complex even = values[start + j];
                double complex odd = values[start + j + length / 2U] * weight;
                values[start + j] = even + odd;
                values[start + j + length / 2U] = even - odd;
                weight *= root;
            }
        }
    }
}

static int fft_calculate(const struct sparam_frame *frame,
                         struct sparam_result *result)
{
    /*
     * 在入射通道最强频点处比较三路频谱，保持参考频点一致。
     * 当前生成器每帧旋转 7 圈，理想输入会使 peak=7；代码不硬编码 7，
     * 而是扫描入射频谱自行找峰值，以适应不同的有效输入。
     */
    double complex incident[SPARAM_MAX_SAMPLES];
    double complex reflected[SPARAM_MAX_SAMPLES];
    double complex transmitted[SPARAM_MAX_SAMPLES];
    size_t i, peak = 0;

    /* radix-2 FFT 只支持 2 的幂，项目也只验证过 256/512/1024 点。 */
    if (!is_power_of_two(frame->sample_count) ||
        (frame->sample_count != 256U && frame->sample_count != 512U &&
         frame->sample_count != 1024U)) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < frame->sample_count; ++i) {
        incident[i] = frame->samples[i].incident;
        reflected[i] = frame->samples[i].reflected;
        transmitted[i] = frame->samples[i].transmitted;
    }
    fft(incident, frame->sample_count);
    fft(reflected, frame->sample_count);
    fft(transmitted, frame->sample_count);
    for (i = 1; i < frame->sample_count; ++i) {
        if (cabs(incident[i]) > cabs(incident[peak]))
            peak = i;
    }
    if (cabs(incident[peak]) < SPARAM_EPSILON) {
        errno = ERANGE;
        return -1;
    }
    fill_result(reflected[peak] / incident[peak],
                transmitted[peak] / incident[peak], result);
    return 0;
}

const char *sparam_algorithm_name(enum sparam_algorithm algorithm)
{
    return algorithm == SPARAM_ALG_FFT ? "fft" : "correlation";
}

int sparam_parse_algorithm(const char *text, enum sparam_algorithm *algorithm)
{
    if (!text || !algorithm)
        return -1;
    if (!strcasecmp(text, "correlation"))
        *algorithm = SPARAM_ALG_CORRELATION;
    else if (!strcasecmp(text, "fft"))
        *algorithm = SPARAM_ALG_FFT;
    else
        return -1;
    return 0;
}

int sparam_calculate(const struct sparam_frame *frame,
                     enum sparam_algorithm algorithm,
                     struct sparam_result *result)
{
    /*
     * 这是服务层调用的统一算法入口。
     * 它负责校验、计时、分派算法和补齐公共元数据；具体数学在下层函数完成。
     */
    struct timespec start, end;
    int status;

    if (!frame || !result || frame->sample_count < SPARAM_MIN_SAMPLES ||
        frame->sample_count > SPARAM_MAX_SAMPLES) {
        errno = EINVAL;
        return -1;
    }
    /* 单调时钟只测计算耗时，不受系统时间校准影响。 */
    clock_gettime(CLOCK_MONOTONIC, &start);
    status = algorithm == SPARAM_ALG_FFT
               ? fft_calculate(frame, result)
               : correlation_calculate(frame, result);
    clock_gettime(CLOCK_MONOTONIC, &end);
    result->frequency_hz = frame->frequency_hz;
    result->algorithm = algorithm;
    result->elapsed_us = elapsed_us(&start, &end);
    result->status = status;
    return status;
}
