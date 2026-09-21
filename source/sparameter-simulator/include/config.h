#ifndef CONFIG_H
#define CONFIG_H

/* 从可选 key=value 文件解析出的运行配置。 */
#include <stddef.h>
#include "log.h"
#include "sparam.h"

enum source_type {
    /* 两种数据源都会先统一为 sparam_frame，再进入算法。 */
    SOURCE_CSV = 0,
    SOURCE_DEVICE = 1,
};

struct app_config {
    /* 两种可选输入及共享结果输出的路径。 */
    enum source_type source;
    char input_path[256];
    char output_path[256];
    char device_path[256];
    unsigned short port;
    size_t queue_length;
    enum sparam_algorithm algorithm;
    enum log_level log_level;
};

void config_defaults(struct app_config *config);
int config_load(struct app_config *config, const char *path,
                unsigned long *error_line);

#endif
