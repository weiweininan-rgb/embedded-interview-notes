#ifndef SWEEP_H
#define SWEEP_H

/* 频率相关模拟器和扫频 trace 共用的数据合同。 */
#include "sparam.h"

#include <complex.h>
#include <stddef.h>

#define SPARAM_MAX_TRACE_POINTS 4096U

enum simulator_scenario {
    SCENARIO_LOAD = 0,
    SCENARIO_OPEN,
    SCENARIO_SHORT,
    SCENARIO_THRU,
    SCENARIO_ATTENUATOR_6DB,
    SCENARIO_RICE_DEMO,
    SCENARIO_MUNG_BEAN_DEMO,
    SCENARIO_INVALID_TRACE,
};

struct sweep_config {
    double start_hz;
    double stop_hz;
    double step_hz;
    size_t sample_count;
    enum simulator_scenario scenario;
};

struct simulator_error {
    /* 单端口三项反射误差和一项传输误差，所有量都是复数。 */
    double complex directivity;
    double complex reflection_tracking;
    double complex source_match;
    double complex transmission_tracking;
    /* 仅用于运行时演示的轻微扫频差异；0 表示完全可重复的测试模型。 */
    double variation_phase;
};

struct trace_point {
    double frequency_hz;
    double complex s11;
    double complex s21;
};

struct sparam_trace {
    struct sweep_config config;
    struct trace_point *points;
    size_t point_count;
};

const char *simulator_scenario_name(enum simulator_scenario scenario);
int simulator_parse_scenario(const char *text, enum simulator_scenario *scenario);
void simulator_default_error(struct simulator_error *error);
int simulator_generate_frame(enum simulator_scenario scenario,
                             double frequency_hz, size_t sample_count,
                             const struct simulator_error *error,
                             struct sparam_frame *frame);

int sweep_config_validate(const struct sweep_config *config, size_t *point_count);
int trace_allocate(struct sparam_trace *trace, const struct sweep_config *config,
                   size_t point_count);
void trace_free(struct sparam_trace *trace);
int trace_copy(struct sparam_trace *destination, const struct sparam_trace *source);
int trace_write_csv(const char *path, const struct sparam_trace *trace);

/* 同步执行接口用于单元测试；服务层仍用现有生产者/消费者线程组织扫频。 */
int sweep_run(const struct sweep_config *config, enum sparam_algorithm algorithm,
              const struct simulator_error *error, struct sparam_trace *trace);

#endif
