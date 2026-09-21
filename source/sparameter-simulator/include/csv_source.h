#ifndef CSV_SOURCE_H
#define CSV_SOURCE_H

/*
 * 严格 CSV 读取器：把相邻同频率行组合为一帧。
 * 一行是一个采样时刻；同一 frequency_hz 下、sample_index 连续的多行
 * 才组成一个可交给算法的 sparam_frame。
 */
#include <stdio.h>
#include "sparam.h"

struct csv_source {
    FILE *file;
    char path[256];
    unsigned long line_number;
    /*
     * 读到下一频率的第 0 点时，当前帧已经完成。
     * 该首行不能丢弃，也不能写入旧帧，因此先暂存并在下次调用时取出。
     */
    int has_pending;
    double pending_frequency;
    struct sparam_sample pending_sample;
};

int csv_source_open(struct csv_source *source, const char *path);
/*
 * 返回 1 表示 frame 已填好一帧，0 表示所有数据已读完，-1 表示格式或 I/O 错误。
 * 调用者应在返回 1 时处理 frame，并继续调用直到得到 0 或 -1。
 */
int csv_source_next(struct csv_source *source, struct sparam_frame *frame);
void csv_source_close(struct csv_source *source);
int csv_result_open(FILE **file, const char *path);
int csv_result_write(FILE *file, const struct sparam_result *result);

#endif
