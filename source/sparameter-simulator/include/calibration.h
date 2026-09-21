#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "sweep.h"

struct calibration_point {
    double frequency_hz;
    double complex directivity;
    double complex reflection_tracking;
    double complex source_match;
    double complex transmission_tracking;
};

struct calibration_table {
    struct sweep_config config;
    struct calibration_point *points;
    size_t point_count;
    int valid;
};

void calibration_free(struct calibration_table *table);
int calibration_build(const struct sparam_trace *load,
                      const struct sparam_trace *open,
                      const struct sparam_trace *short_trace,
                      const struct sparam_trace *thru,
                      struct calibration_table *table);
int calibration_apply(const struct calibration_table *table,
                      const struct sparam_trace *raw,
                      struct sparam_trace *calibrated);

#endif
