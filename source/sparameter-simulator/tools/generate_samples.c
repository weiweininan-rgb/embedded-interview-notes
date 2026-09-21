/* 生成具有已知复数 S11/S21 比值的确定性 IQ 测试数据。 */
#include <complex.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

int main(int argc, char **argv)
{
    FILE *file;
    char *end;
    unsigned long count, i;
    /* 对应预期的 -6.0206 dB/+30° 与 -12.0412 dB/-45°。 */
    double complex s11 = 0.5 * cexp(I * PI / 6.0);
    double complex s21 = 0.25 * cexp(-I * PI / 4.0);

    if (argc != 3) {
        fprintf(stderr, "usage: %s OUTPUT 256|512|1024\n", argv[0]);
        return 2;
    }
    count = strtoul(argv[2], &end, 10);
    if (*end || (count != 256 && count != 512 && count != 1024)) {
        fprintf(stderr, "invalid sample count\n");
        return 2;
    }
    file = fopen(argv[1], "w");
    if (!file) { perror(argv[1]); return 1; }
    fprintf(file, "frequency_hz,sample_index,incident_re,incident_im,reflected_re,reflected_im,transmitted_re,transmitted_im\n");
    for (i = 0; i < count; ++i) {
        double angle = 2.0 * PI * 7.0 * (double)i / (double)count;
        /* 三路共享同一基准音调，乘 S11/S21 后得到精确目标比值。 */
        double complex incident = cexp(I * angle);
        double complex reflected = incident * s11;
        double complex transmitted = incident * s21;
        fprintf(file, "100000000,%lu,%.12f,%.12f,%.12f,%.12f,%.12f,%.12f\n",
                i, creal(incident), cimag(incident), creal(reflected),
                cimag(reflected), creal(transmitted), cimag(transmitted));
    }
    if (fclose(file)) { perror("fclose"); return 1; }
    return 0;
}
