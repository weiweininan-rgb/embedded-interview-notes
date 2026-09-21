#define _POSIX_C_SOURCE 200809L
/*
 * CSV 数据路径：文本行 -> sparam_sample -> 同频 sparam_frame。
 * 本文件只负责语法、顺序和帧边界；S11/S21 算法完全不在此处出现。
 */
#include "csv_source.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CSV_LINE_MAX 512
#define CSV_HEADER "frequency_hz,sample_index,incident_re,incident_im,reflected_re,reflected_im,transmitted_re,transmitted_im"

static int parse_line(char *line, double *frequency, unsigned long *index,
                      struct sparam_sample *sample)
{
    /*
     * 输入列必须恰好为 frequency、index 和三路 IQ 的实虚部共 8 项。
     * 末尾 %c 只有遇到额外非空白字符才会成功，因此 fields 必须严格等于 8。
     */
    char extra;
    double ir, ii, rr, ri, tr, ti;
    int fields = sscanf(line, " %lf , %lu , %lf , %lf , %lf , %lf , %lf , %lf %c",
                        frequency, index, &ir, &ii, &rr, &ri, &tr, &ti, &extra);
    if (fields != 8 || !isfinite(*frequency) || !isfinite(ir) || !isfinite(ii) ||
        !isfinite(rr) || !isfinite(ri) || !isfinite(tr) || !isfinite(ti))
        return -1;
    sample->incident = ir + I * ii;
    sample->reflected = rr + I * ri;
    sample->transmitted = tr + I * ti;
    return 0;
}

int csv_source_open(struct csv_source *source, const char *path)
{
    /*
     * 先校验精确表头，再允许读数据；否则“第 5 列到底是不是反射实部”
     * 会失去确定性，后面的复数计算即使正常执行也没有物理意义。
     */
    char header[CSV_LINE_MAX];
    if (!source || !path || strlen(path) >= sizeof(source->path)) {
        errno = EINVAL;
        return -1;
    }
    memset(source, 0, sizeof(*source));
    source->file = fopen(path, "r");
    if (!source->file)
        return -1;
    strcpy(source->path, path);
    if (!fgets(header, sizeof(header), source->file)) {
        errno = EINVAL;
        csv_source_close(source);
        return -1;
    }
    header[strcspn(header, "\r\n")] = '\0';
    if (strcmp(header, CSV_HEADER)) {
        errno = EINVAL;
        csv_source_close(source);
        return -1;
    }
    source->line_number = 1;
    return 0;
}

int csv_source_next(struct csv_source *source, struct sparam_frame *frame)
{
    /*
     * 每次调用最多交付一帧：从 index=0 开始读取连续同频样本。
     * 遇到下一频率的 index=0 时，保存这一行并返回旧帧；下一次调用再用它开帧。
     */
    char line[CSV_LINE_MAX];
    unsigned long expected_index = 0;

    if (!source || !source->file || !frame) {
        errno = EINVAL;
        return -1;
    }
    /* 清零使 sample_count 从 0 开始，也避免未写入槽位保留旧帧残值。 */
    memset(frame, 0, sizeof(*frame));
    /* 先取上次跨频率边界时暂存的那一行。 */
    if (source->has_pending) {
        frame->frequency_hz = source->pending_frequency;
        frame->samples[0] = source->pending_sample;
        frame->sample_count = 1;
        source->has_pending = 0;
        expected_index = 1;
    }
    while (fgets(line, sizeof(line), source->file)) {
        double frequency;
        unsigned long index;
        struct sparam_sample sample;
        source->line_number++;
        if (!strchr(line, '\n') && !feof(source->file)) {
            errno = EOVERFLOW;
            return -1;
        }
        if (parse_line(line, &frequency, &index, &sample) == -1) {
            errno = EINVAL;
            return -1;
        }
        /* 第一条有效样本决定这一帧的频率。 */
        if (frame->sample_count == 0)
            frame->frequency_hz = frequency;
        /* 新频率的首行属于下一帧，不能混入当前帧。 */
        if (frequency != frame->frequency_hz) {
            if (index != 0) { errno = EINVAL; return -1; }
            source->pending_frequency = frequency;
            source->pending_sample = sample;
            source->has_pending = 1;
            return 1;
        }
        /* 禁止跳号、重复或乱序，确保 samples[i] 就是 CSV 的第 i 个时刻。 */
        if (index != expected_index) { errno = EINVAL; return -1; }
        if (frame->sample_count == SPARAM_MAX_SAMPLES) {
            errno = EOVERFLOW;
            return -1;
        }
        frame->samples[frame->sample_count++] = sample;
        expected_index++;
    }
    if (ferror(source->file))
        return -1;
    /* EOF 前已经积累到样本时，仍需把这最后一帧交给调用者。 */
    return frame->sample_count ? 1 : 0;
}

void csv_source_close(struct csv_source *source)
{
    if (source && source->file)
        fclose(source->file);
    if (source)
        source->file = NULL;
}

int csv_result_open(FILE **file, const char *path)
{
    /* 结果使用稳定列格式，便于脚本和后续 GUI 导入。 */
    if (!file || !path) {
        errno = EINVAL;
        return -1;
    }
    *file = fopen(path, "w");
    if (!*file)
        return -1;
    if (fprintf(*file, "frequency_hz,algorithm,s11_mag_db,s11_phase_deg,"
                       "s21_mag_db,s21_phase_deg,elapsed_us,status\n") < 0) {
        fclose(*file);
        *file = NULL;
        return -1;
    }
    return 0;
}

int csv_result_write(FILE *file, const struct sparam_result *result)
{
    /* 连同算法名和状态写出，保证结果可追溯。 */
    if (!file || !result) {
        errno = EINVAL;
        return -1;
    }
    return fprintf(file, "%.0f,%s,%.9f,%.9f,%.9f,%.9f,%llu,%s\n",
                   result->frequency_hz, sparam_algorithm_name(result->algorithm),
                   result->s11_mag_db, result->s11_phase_deg,
                   result->s21_mag_db, result->s21_phase_deg,
                   (unsigned long long)result->elapsed_us,
                   result->status == 0 ? "ok" : "error") < 0 ? -1 : 0;
}
