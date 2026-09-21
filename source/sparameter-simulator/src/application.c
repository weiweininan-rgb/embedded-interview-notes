#include "application.h"

#include <errno.h>
#include <math.h>
#include <strings.h>

const char *grain_type_name(enum grain_type grain)
{
    if (grain == GRAIN_RICE) return "rice";
    if (grain == GRAIN_MUNG_BEAN) return "mung_bean";
    return "none";
}

int grain_parse(const char *text, enum grain_type *grain)
{
    if (!text || !grain)
        return -1;
    if (!strcasecmp(text, "none")) *grain = GRAIN_NONE;
    else if (!strcasecmp(text, "rice")) *grain = GRAIN_RICE;
    else if (!strcasecmp(text, "mung_bean")) *grain = GRAIN_MUNG_BEAN;
    else return -1;
    return 0;
}

static double crossing_frequency(const struct trace_point *low,
                                 const struct trace_point *high,
                                 double target)
{
    double low_db = 20.0 * log10(cabs(low->s21));
    double high_db = 20.0 * log10(cabs(high->s21));
    double target_db = 20.0 * log10(target);
    double fraction;
    if (fabs(high_db - low_db) < 1e-12)
        return (low->frequency_hz + high->frequency_hz) / 2.0;
    fraction = (target_db - low_db) / (high_db - low_db);
    return low->frequency_hz + fraction *
           (high->frequency_hz - low->frequency_hz);
}

int resonance_analyze(const struct sparam_trace *trace,
                      struct resonance_result *result)
{
    size_t peak = 0, index;
    double peak_magnitude, target;
    int left_found = 0, right_found = 0;
    if (!trace || !trace->points || trace->point_count < 3 || !result) {
        errno = EINVAL;
        return -1;
    }
    for (index = 1; index < trace->point_count; ++index) {
        if (cabs(trace->points[index].s21) > cabs(trace->points[peak].s21))
            peak = index;
    }
    if (peak == 0 || peak + 1U == trace->point_count) {
        errno = ERANGE;
        return -1;
    }
    peak_magnitude = cabs(trace->points[peak].s21);
    target = peak_magnitude / sqrt(2.0);
    for (index = peak; index > 0; --index) {
        if (cabs(trace->points[index - 1U].s21) <= target &&
            cabs(trace->points[index].s21) >= target) {
            result->f1_hz = crossing_frequency(&trace->points[index - 1U],
                                                &trace->points[index], target);
            left_found = 1;
            break;
        }
    }
    for (index = peak; index + 1U < trace->point_count; ++index) {
        if (cabs(trace->points[index].s21) >= target &&
            cabs(trace->points[index + 1U].s21) <= target) {
            result->f2_hz = crossing_frequency(&trace->points[index],
                                                &trace->points[index + 1U], target);
            right_found = 1;
            break;
        }
    }
    if (!left_found || !right_found || result->f2_hz <= result->f1_hz) {
        errno = ERANGE;
        return -1;
    }
    result->f0_hz = trace->points[peak].frequency_hz;
    result->q = result->f0_hz / (result->f2_hz - result->f1_hz);
    return 0;
}

int moisture_calculate(enum grain_type grain, double f0_hz, double q,
                       double *moisture_percent)
{
    double value;
    if (!moisture_percent || !isfinite(f0_hz) || !isfinite(q) ||
        f0_hz <= 0.0 || q <= 0.0) {
        errno = EINVAL;
        return -1;
    }
    if (grain == GRAIN_RICE) {
        /* 论文图 6-10 的拟合样本 Q 大致位于 35～110。 */
        if (q < 35.0 || q > 110.0) {
            errno = ERANGE;
            return -1;
        }
        value = 35.5329 - 0.9227 * q + 0.0093 * q * q -
                3.2699e-5 * q * q * q;
    } else if (grain == GRAIN_MUNG_BEAN) {
        double frequency_mhz = f0_hz / 1e6;
        /* 式 (5-3) 的系数对应 MHz；GHz 会产生数百%的明显错误结果。 */
        if (frequency_mhz < 1400.0 || frequency_mhz > 1550.0) {
            errno = ERANGE;
            return -1;
        }
        value = 274.727 - 0.177 * frequency_mhz - 0.068 * q;
    } else {
        errno = EINVAL;
        return -1;
    }
    if (!isfinite(value) || value < 0.0 || value > 100.0) {
        errno = ERANGE;
        return -1;
    }
    *moisture_percent = value;
    return 0;
}
