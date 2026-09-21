/* 使用项目通用的已知复数比 IQ 数据做算法回归测试。 */
#include "sparam.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdio.h>

#define PI 3.14159265358979323846

static int close_to(double actual, double expected, double tolerance)
{
    return fabs(actual - expected) <= tolerance;
}

static void fill_tone(struct sparam_frame *frame, size_t count, size_t bin)
{
    /* 把音调放在精确 FFT bin 上，两种算法应恢复同一复数比。 */
    size_t i;
    double complex s11 = 0.5 * cexp(I * PI / 6.0);
    double complex s21 = 0.25 * cexp(-I * PI / 4.0);
    frame->frequency_hz = 100000000.0;
    frame->sample_count = count;
    for (i = 0; i < count; ++i) {
        double angle = 2.0 * PI * (double)(bin * i) / (double)count;
        frame->samples[i].incident = cexp(I * angle);
        frame->samples[i].reflected = frame->samples[i].incident * s11;
        frame->samples[i].transmitted = frame->samples[i].incident * s21;
    }
}

int main(void)
{
    struct sparam_frame frame;
    struct sparam_result result;

    fill_tone(&frame, 256, 7);
    assert(sparam_calculate(&frame, SPARAM_ALG_CORRELATION, &result) == 0);
    assert(close_to(result.s11_mag_db, -6.020599913, 1e-6));
    assert(close_to(result.s11_phase_deg, 30.0, 1e-6));
    assert(close_to(result.s21_mag_db, -12.041199827, 1e-6));
    assert(close_to(result.s21_phase_deg, -45.0, 1e-6));

    assert(sparam_calculate(&frame, SPARAM_ALG_FFT, &result) == 0);
    assert(close_to(result.s11_mag_db, -6.020599913, 1e-6));
    assert(close_to(result.s11_phase_deg, 30.0, 1e-6));
    assert(close_to(result.s21_phase_deg, -45.0, 1e-6));

    /* FFT 只允许文档规定的 256/512/1024 点数。 */
    frame.sample_count = 128;
    assert(sparam_calculate(&frame, SPARAM_ALG_FFT, &result) == -1);
    puts("algorithm tests: PASS");
    return 0;
}
