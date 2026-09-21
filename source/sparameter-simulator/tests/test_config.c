#define _POSIX_C_SOURCE 200809L
/* 验证默认配置，以及合法/非法 key=value 配置的边界。 */
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int load_text(const char *text, struct app_config *config,
                     unsigned long *error_line)
{
    /* 使用唯一临时文件，避免测试依赖仓库中的真实配置。 */
    static unsigned int sequence;
    char path[128];
    FILE *file;
    int result;

    snprintf(path, sizeof(path), "sparam-config-test-%ld-%u.conf",
             (long)getpid(), sequence++);
    file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);

    result = config_load(config, path, error_line);
    assert(unlink(path) == 0);
    return result;
}

static void test_valid_config(void)
{
    struct app_config config;
    unsigned long error_line = 0;

    config_defaults(&config);
    assert(load_text("# comment\n\nsource=csv\n"
                     "input_path=data/in.csv\n"
                     "output_path=results/out.csv\n"
                     "device_path=/dev/test\n"
                     "port=19000\nqueue_length=2\n"
                     "algorithm=fft\nlog_level=debug\n",
                     &config, &error_line) == 0);
    assert(config.source == SOURCE_CSV);
    assert(strcmp(config.input_path, "data/in.csv") == 0);
    assert(strcmp(config.output_path, "results/out.csv") == 0);
    assert(strcmp(config.device_path, "/dev/test") == 0);
    assert(config.port == 19000);
    assert(config.queue_length == 2);
    assert(config.algorithm == SPARAM_ALG_FFT);
    assert(config.log_level == LOG_DEBUG);
}

static void test_missing_file(void)
{
    struct app_config config;
    unsigned long error_line = 0;

    config_defaults(&config);
    errno = 0;
    assert(config_load(&config, "sparam-config-does-not-exist.conf", &error_line) == -1);
}

static void test_invalid_configs(void)
{
    /* 每个用例覆盖一条解析保护规则，不能接受半合法配置。 */
    struct app_config config;
    unsigned long error_line;

    config_defaults(&config);
    error_line = 0;
    assert(load_text("source=csv\ninvalid-line\n", &config, &error_line) == -1);
    assert(error_line == 2);

    config_defaults(&config);
    assert(load_text("port=99999\n", &config, NULL) == -1);
    config_defaults(&config);
    assert(load_text("port=abc\n", &config, NULL) == -1);

    config_defaults(&config);
    assert(load_text("queue_length=0\n", &config, NULL) == -1);
    config_defaults(&config);
    assert(load_text("queue_length=999\n", &config, NULL) == -1);

    config_defaults(&config);
    assert(load_text("algorithm=unknown\n", &config, NULL) == -1);
    config_defaults(&config);
    assert(load_text("foo=bar\n", &config, NULL) == -1);
    config_defaults(&config);
    assert(load_text("log_level=verbose\n", &config, NULL) == -1);
}

static void test_log_level_names(void)
{
    static const char *names[] = { "debug", "info", "warn", "error" };
    static const enum log_level levels[] = {
        LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR
    };
    struct app_config config;
    char text[32];
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        config_defaults(&config);
        snprintf(text, sizeof(text), "log_level=%s\n", names[i]);
        assert(load_text(text, &config, NULL) == 0);
        assert(config.log_level == levels[i]);
    }
}

int main(void)
{
    test_valid_config();
    test_missing_file();
    test_invalid_configs();
    test_log_level_names();
    puts("config tests: PASS");
    return 0;
}
