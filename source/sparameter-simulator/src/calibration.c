#include "calibration.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int same_grid(const struct sparam_trace *left,
                     const struct sparam_trace *right)
{
    size_t index;
    if (!left || !right || !left->points || !right->points ||
        left->point_count != right->point_count)
        return 0;
    for (index = 0; index < left->point_count; ++index) {
        if (fabs(left->points[index].frequency_hz -
                 right->points[index].frequency_hz) > 0.5)
            return 0;
    }
    return 1;
}

void calibration_free(struct calibration_table *table)
{
    if (!table)
        return;
    free(table->points);
    memset(table, 0, sizeof(*table));
}

int calibration_build(const struct sparam_trace *load,
                      const struct sparam_trace *open,
                      const struct sparam_trace *short_trace,
                      const struct sparam_trace *thru,
                      struct calibration_table *table)
{
    struct calibration_point *points;
    size_t index;
    if (!table || !same_grid(load, open) || !same_grid(load, short_trace) ||
        !same_grid(load, thru)) {
        errno = EINVAL;
        return -1;
    }
    points = calloc(load->point_count, sizeof(*points));
    if (!points)
        return -1;
    for (index = 0; index < load->point_count; ++index) {
        double complex directivity = load->points[index].s11;
        double complex open_delta = open->points[index].s11 - directivity;
        double complex short_delta = short_trace->points[index].s11 - directivity;
        double complex denominator = open_delta - short_delta;
        double complex source_match, tracking;
        if (cabs(denominator) < 1e-12 || cabs(thru->points[index].s21) < 1e-12) {
            free(points);
            errno = ERANGE;
            return -1;
        }
        /* 由 Γopen=+1、Γshort=-1 解出三项反射误差。 */
        source_match = (open_delta + short_delta) / denominator;
        tracking = open_delta * (1.0 - source_match);
        points[index].frequency_hz = load->points[index].frequency_hz;
        points[index].directivity = directivity;
        points[index].reflection_tracking = tracking;
        points[index].source_match = source_match;
        points[index].transmission_tracking = thru->points[index].s21;
    }
    calibration_free(table);
    table->config = load->config;
    table->points = points;
    table->point_count = load->point_count;
    table->valid = 1;
    return 0;
}

int calibration_apply(const struct calibration_table *table,
                      const struct sparam_trace *raw,
                      struct sparam_trace *calibrated)
{
    size_t index;
    if (!table || !table->valid || !table->points || !raw || !raw->points ||
        table->point_count != raw->point_count) {
        errno = EINVAL;
        return -1;
    }
    trace_free(calibrated);
    if (trace_allocate(calibrated, &raw->config, raw->point_count))
        return -1;
    for (index = 0; index < raw->point_count; ++index) {
        const struct calibration_point *cal = &table->points[index];
        double complex delta, denominator;
        if (fabs(cal->frequency_hz - raw->points[index].frequency_hz) > 0.5) {
            trace_free(calibrated);
            errno = EINVAL;
            return -1;
        }
        delta = raw->points[index].s11 - cal->directivity;
        denominator = cal->reflection_tracking + cal->source_match * delta;
        if (cabs(denominator) < 1e-12 ||
            cabs(cal->transmission_tracking) < 1e-12) {
            trace_free(calibrated);
            errno = ERANGE;
            return -1;
        }
        calibrated->points[index].frequency_hz = raw->points[index].frequency_hz;
        calibrated->points[index].s11 = delta / denominator;
        calibrated->points[index].s21 = raw->points[index].s21 /
                                         cal->transmission_tracking;
    }
    return 0;
}
