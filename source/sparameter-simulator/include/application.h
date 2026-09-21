#ifndef APPLICATION_H
#define APPLICATION_H

#include "sweep.h"

enum grain_type {
    GRAIN_NONE = 0,
    GRAIN_RICE,
    GRAIN_MUNG_BEAN,
};

struct resonance_result {
    double f0_hz;
    double f1_hz;
    double f2_hz;
    double q;
};

const char *grain_type_name(enum grain_type grain);
int grain_parse(const char *text, enum grain_type *grain);
int resonance_analyze(const struct sparam_trace *trace,
                      struct resonance_result *result);
int moisture_calculate(enum grain_type grain, double f0_hz, double q,
                       double *moisture_percent);

#endif
