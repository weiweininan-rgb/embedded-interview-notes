#define _POSIX_C_SOURCE 200809L
/* 用相同模拟帧比较算法 CPU 耗时；这不是射频性能。 */
#include "sparam.h"

#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define PI 3.14159265358979323846
#define ITERATIONS 200 /* 多次重复以减小计时抖动，结果写入 CSV。 */

static void fill_frame(struct sparam_frame *frame, size_t count)
{
    /* 复用已知比值音调，使基准输出也能顺带检查正确性。 */
    size_t i;
    double complex s11 = 0.5 * cexp(I * PI / 6.0);
    double complex s21 = 0.25 * cexp(-I * PI / 4.0);
    frame->frequency_hz = 100000000.0;
    frame->sample_count = count;
    for (i = 0; i < count; ++i) {
        double angle = 2.0 * PI * 7.0 * (double)i / (double)count;
        frame->samples[i].incident = cexp(I * angle);
        frame->samples[i].reflected = frame->samples[i].incident * s11;
        frame->samples[i].transmitted = frame->samples[i].incident * s21;
    }
}

int main(void)
{
    static const size_t counts[] = {256, 512, 1024};
    struct sparam_frame *frame = malloc(sizeof(*frame));
    size_t c;
    int algorithm, iteration;
    if (!frame) return 1;
    puts("samples,algorithm,iterations,total_us,average_us,s11_db,s21_db");
    for (c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c) {
        fill_frame(frame, counts[c]);
        for (algorithm = SPARAM_ALG_CORRELATION; algorithm <= SPARAM_ALG_FFT; ++algorithm) {
            struct timespec start, end;
            double total_us;
            struct sparam_result result = {0};
            /* 只测重复计算，不包含构帧和打印耗时。 */
            clock_gettime(CLOCK_MONOTONIC, &start);
            for (iteration = 0; iteration < ITERATIONS; ++iteration) {
                if (sparam_calculate(frame, algorithm, &result)) { free(frame); return 1; }
            }
            clock_gettime(CLOCK_MONOTONIC, &end);
            total_us = (end.tv_sec - start.tv_sec) * 1000000.0 +
                       (end.tv_nsec - start.tv_nsec) / 1000.0;
            printf("%zu,%s,%d,%.3f,%.3f,%.9f,%.9f\n", counts[c],
                   sparam_algorithm_name(algorithm), ITERATIONS, total_us,
                   total_us / ITERATIONS, result.s11_mag_db, result.s21_mag_db);
        }
    }
    free(frame);
    return 0;
}
