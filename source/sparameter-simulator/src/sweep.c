#include "sweep.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sweep_config_validate(const struct sweep_config *config, size_t *point_count)
{
    double intervals, rounded;
    size_t count;
    if (!config || !isfinite(config->start_hz) || !isfinite(config->stop_hz) ||
        !isfinite(config->step_hz) || config->start_hz <= 0.0 ||
        config->stop_hz < config->start_hz || config->step_hz <= 0.0 ||
        config->sample_count < SPARAM_MIN_SAMPLES ||
        config->sample_count > SPARAM_MAX_SAMPLES ||
        config->scenario < SCENARIO_LOAD || config->scenario > SCENARIO_INVALID_TRACE) {
        errno = EINVAL;
        return -1;
    }
    intervals = (config->stop_hz - config->start_hz) / config->step_hz;
    rounded = round(intervals);
    /* 终点必须正好落在步进网格上，避免最后一段频率间隔不一致。 */
    if (fabs(intervals - rounded) > 1e-7 * fmax(1.0, fabs(intervals))) {
        errno = EINVAL;
        return -1;
    }
    count = (size_t)rounded + 1U;
    if (!count || count > SPARAM_MAX_TRACE_POINTS) {
        errno = E2BIG;
        return -1;
    }
    if (point_count)
        *point_count = count;
    return 0;
}

int trace_allocate(struct sparam_trace *trace, const struct sweep_config *config,
                   size_t point_count)
{
    if (!trace || !config || !point_count || point_count > SPARAM_MAX_TRACE_POINTS) {
        errno = EINVAL;
        return -1;
    }
    memset(trace, 0, sizeof(*trace));
    trace->points = calloc(point_count, sizeof(*trace->points));
    if (!trace->points)
        return -1;
    trace->config = *config;
    trace->point_count = point_count;
    return 0;
}

void trace_free(struct sparam_trace *trace)
{
    if (!trace)
        return;
    free(trace->points);
    memset(trace, 0, sizeof(*trace));
}

int trace_copy(struct sparam_trace *destination, const struct sparam_trace *source)
{
    if (!destination || !source || !source->points || !source->point_count) {
        errno = EINVAL;
        return -1;
    }
    trace_free(destination);
    if (trace_allocate(destination, &source->config, source->point_count))
        return -1;
    memcpy(destination->points, source->points,
           source->point_count * sizeof(*source->points));
    return 0;
}

int trace_write_csv(const char *path, const struct sparam_trace *trace)
{
    FILE *file;
    size_t index;
    if (!path || !trace || !trace->points) {
        errno = EINVAL;
        return -1;
    }
    file = fopen(path, "w");
    if (!file)
        return -1;
    fputs("frequency_hz,s11_re,s11_im,s21_re,s21_im\n", file);
    for (index = 0; index < trace->point_count; ++index) {
        const struct trace_point *point = &trace->points[index];
        if (fprintf(file, "%.0f,%.12g,%.12g,%.12g,%.12g\n",
                    point->frequency_hz, creal(point->s11), cimag(point->s11),
                    creal(point->s21), cimag(point->s21)) < 0) {
            fclose(file);
            return -1;
        }
    }
    return fclose(file) ? -1 : 0;
}

int sweep_run(const struct sweep_config *config, enum sparam_algorithm algorithm,
              const struct simulator_error *error, struct sparam_trace *trace)
{
    struct sparam_frame frame;
    size_t count, index;
    if (!trace || sweep_config_validate(config, &count))
        return -1;
    trace_free(trace);
    if (trace_allocate(trace, config, count))
        return -1;
    for (index = 0; index < count; ++index) {
        struct sparam_result result;
        double frequency = config->start_hz + config->step_hz * (double)index;
        if (simulator_generate_frame(config->scenario, frequency,
                                     config->sample_count, error, &frame) ||
            sparam_calculate(&frame, algorithm, &result)) {
            trace_free(trace);
            return -1;
        }
        trace->points[index].frequency_hz = frequency;
        trace->points[index].s11 = result.s11;
        trace->points[index].s21 = result.s21;
    }
    return 0;
}
