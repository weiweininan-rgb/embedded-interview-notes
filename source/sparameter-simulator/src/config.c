#define _POSIX_C_SOURCE 200809L
/* 解析刻意保持简单的 key=value 配置格式。 */
#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *text)
{
    /* 校验键和值之前，先统一处理首尾空白。 */
    char *end;
    while (*text == ' ' || *text == '\t')
        text++;
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\n' || end[-1] == '\r'))
        *--end = '\0';
    return text;
}

void config_defaults(struct app_config *config)
{
    /* 每项都有默认值，因此配置文件缺失时仍可运行。 */
    memset(config, 0, sizeof(*config));
    config->source = SOURCE_CSV;
    strcpy(config->input_path, "data/samples_256.csv");
    strcpy(config->output_path, "results/output.csv");
    strcpy(config->device_path, "/dev/sparam_sim");
    config->port = 9000;
    config->queue_length = 4;
    config->algorithm = SPARAM_ALG_CORRELATION;
    config->log_level = LOG_INFO;
}

static int copy_value(char *destination, size_t capacity, const char *value)
{
    /* 拒绝截断，避免路径被悄悄改写。 */
    if (strlen(value) >= capacity)
        return -1;
    strcpy(destination, value);
    return 0;
}

int config_load(struct app_config *config, const char *path,
                unsigned long *error_line)
{
    FILE *file;
    char line[512];
    unsigned long number = 0;

    if (!config || !path) {
        errno = EINVAL;
        return -1;
    }
    file = fopen(path, "r");
    if (!file)
        return -1;
    while (fgets(line, sizeof(line), file)) {
        char *key, *value, *separator;
        char *end;
        unsigned long parsed;
        number++;
        key = trim(line);
        if (!*key || *key == '#')
            continue;
        /* 每行只能是空行、注释，或一条完整 key=value 配置。 */
        separator = strchr(key, '=');
        if (!separator)
            goto invalid;
        *separator = '\0';
        value = trim(separator + 1);
        key = trim(key);
        if (!strcmp(key, "source")) {
            if (!strcmp(value, "csv")) config->source = SOURCE_CSV;
            else if (!strcmp(value, "device")) config->source = SOURCE_DEVICE;
            else goto invalid;
        } else if (!strcmp(key, "input_path")) {
            if (copy_value(config->input_path, sizeof(config->input_path), value)) goto invalid;
        } else if (!strcmp(key, "output_path")) {
            if (copy_value(config->output_path, sizeof(config->output_path), value)) goto invalid;
        } else if (!strcmp(key, "device_path")) {
            if (copy_value(config->device_path, sizeof(config->device_path), value)) goto invalid;
        } else if (!strcmp(key, "port")) {
            parsed = strtoul(value, &end, 10);
            if (*end || parsed == 0 || parsed > 65535) goto invalid;
            config->port = (unsigned short)parsed;
        } else if (!strcmp(key, "queue_length")) {
            parsed = strtoul(value, &end, 10);
            if (*end || parsed == 0 || parsed > 64) goto invalid;
            config->queue_length = (size_t)parsed;
        } else if (!strcmp(key, "algorithm")) {
            if (sparam_parse_algorithm(value, &config->algorithm)) goto invalid;
        } else if (!strcmp(key, "log_level")) {
            int ok;
            config->log_level = log_level_from_name(value, &ok);
            if (!ok) goto invalid;
        } else {
            goto invalid;
        }
    }
    fclose(file);
    return 0;
invalid:
    /* 保留实际行号，方便定位和修复错误配置。 */
    if (error_line) *error_line = number;
    fclose(file);
    errno = EINVAL;
    return -1;
}
