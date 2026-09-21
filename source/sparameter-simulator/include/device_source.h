#ifndef DEVICE_SOURCE_H
#define DEVICE_SOURCE_H

/*
 * 教学驱动 /dev/sparam_sim 的用户态适配层。
 * 驱动输出固定宽度的定点结构体；本层负责等待数据、读完整帧，
 * 再转换为与 CSV 路径完全相同的浮点 sparam_frame。
 */
#include "sparam.h"

struct device_source { int fd; }; /* 数据源打开期间持有的文件描述符。 */
int device_source_open(struct device_source *source, const char *path);
/* 打开设备不会自动出帧，必须显式 START，避免客户端只查看状态就触发测量。 */
int device_source_start(struct device_source *source);
/* 返回 1 表示已得到一帧，-1 表示超时、读失败或 ABI 不匹配。 */
int device_source_next(struct device_source *source, struct sparam_frame *frame);
void device_source_close(struct device_source *source);

#endif
