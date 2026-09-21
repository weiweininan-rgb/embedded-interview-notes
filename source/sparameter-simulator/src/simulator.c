#include "sweep.h"

#include <errno.h>
#include <math.h>
#include <strings.h>

#define PI 3.14159265358979323846
#define SIMULATOR_TONE_CYCLES 7.0

static double complex polar_value(double magnitude, double phase_deg)
{
    double phase = phase_deg * PI / 180.0;
    return magnitude * (cos(phase) + I * sin(phase));
}

const char *simulator_scenario_name(enum simulator_scenario scenario)
{
    static const char *names[] = {
        "load", "open", "short", "thru", "attenuator_6db",
        "rice_demo", "mung_bean_demo", "invalid_trace"
    };
    return scenario >= SCENARIO_LOAD && scenario <= SCENARIO_INVALID_TRACE
             ? names[scenario] : "unknown";
}

int simulator_parse_scenario(const char *text, enum simulator_scenario *scenario)
{
    int value;
    if (!text || !scenario)
        return -1;
    for (value = SCENARIO_LOAD; value <= SCENARIO_INVALID_TRACE; ++value) {
        if (!strcasecmp(text, simulator_scenario_name((enum simulator_scenario)value))) {
            *scenario = (enum simulator_scenario)value;
            return 0;
        }
    }
    return -1;
}

void simulator_default_error(struct simulator_error *error)
{
    if (!error)
        return;
    error->directivity = polar_value(0.04, 20.0);
    error->reflection_tracking = polar_value(0.85, -12.0);
    error->source_match = polar_value(0.08, 25.0);
    error->transmission_tracking = polar_value(0.80, 20.0);
    error->variation_phase = 0.0;
}

static double complex resonator_response(double frequency_hz,
                                         double f0_hz, double q)
{
    /* 单峰二阶响应：f0 处幅度为 1，两侧半功率带宽为 f0/Q。 */
    double detuning = 2.0 * q * (frequency_hz - f0_hz) / f0_hz;
    return 1.0 / (1.0 + I * detuning);
}

static int ideal_sparameters(enum simulator_scenario scenario,
                             double frequency_hz,
                             double variation_phase,
                             double complex *s11, double complex *s21)
{
    double ripple = 1.0;
    *s11 = 0.0;
    *s21 = 0.0;
    switch (scenario) {
    case SCENARIO_LOAD: return 0;
    case SCENARIO_OPEN: *s11 = 1.0; return 0;
    case SCENARIO_SHORT: *s11 = -1.0; return 0;
    case SCENARIO_THRU: *s21 = 1.0; return 0;
    case SCENARIO_ATTENUATOR_6DB:
        *s11 = polar_value(0.03, 10.0);
        *s21 = polar_value(pow(10.0, -6.0 / 20.0), -25.0);
        return 0;
    case SCENARIO_RICE_DEMO:
        *s11 = polar_value(0.08, 15.0);
        if (fabs(variation_phase) < 1e-12) {
            *s21 = resonator_response(frequency_hz, 1.500e9, 50.0);
            return 0;
        }
        *s21 = resonator_response(frequency_hz,
                                  1.500e9 + 2.5e5 * sin(variation_phase),
                                  50.0 * (1.0 + 0.02 * cos(variation_phase)));
        ripple += 0.005 * sin((frequency_hz - 1.45e9) / 3.7e6 + variation_phase) +
                  0.001 * sin((frequency_hz - 1.45e9) / 3.1e5 + 1.7 * variation_phase);
        *s21 *= ripple * (1.0 + 0.004 * (frequency_hz - 1.50e9) / 5.0e7);
        *s11 *= 1.0 + 0.025 * sin((frequency_hz - 1.45e9) / 5.3e6 + variation_phase);
        return 0;
    case SCENARIO_MUNG_BEAN_DEMO:
        *s11 = polar_value(0.08, -10.0);
        if (fabs(variation_phase) < 1e-12) {
            *s21 = resonator_response(frequency_hz, 1.4338e9, 36.8);
            return 0;
        }
        *s21 = resonator_response(frequency_hz,
                                  1.4338e9 + 2.0e5 * sin(variation_phase),
                                  36.8 * (1.0 + 0.025 * cos(variation_phase)));
        ripple += 0.006 * sin((frequency_hz - 1.40e9) / 4.1e6 + variation_phase) +
                  0.0012 * sin((frequency_hz - 1.40e9) / 3.4e5 + 1.5 * variation_phase);
        *s21 *= ripple * (1.0 - 0.005 * (frequency_hz - 1.45e9) / 5.0e7);
        *s11 *= 1.0 + 0.03 * sin((frequency_hz - 1.40e9) / 5.7e6 + variation_phase);
        return 0;
    case SCENARIO_INVALID_TRACE:
        *s11 = polar_value(0.05, 0.0);
        *s21 = polar_value(0.5, 0.0);
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

int simulator_generate_frame(enum simulator_scenario scenario,
                             double frequency_hz, size_t sample_count,
                             const struct simulator_error *error,
                             struct sparam_frame *frame)
{
    struct simulator_error defaults;
    double complex ideal_s11, ideal_s21, raw_s11, raw_s21;
    size_t index;

    if (!frame || frequency_hz <= 0.0 || sample_count < SPARAM_MIN_SAMPLES ||
        sample_count > SPARAM_MAX_SAMPLES) {
        errno = EINVAL;
        return -1;
    }
    if (!error) {
        simulator_default_error(&defaults);
        error = &defaults;
    }
    if (ideal_sparameters(scenario, frequency_hz, error->variation_phase,
                          &ideal_s11, &ideal_s21))
        return -1;
    if (cabs(1.0 - error->source_match * ideal_s11) < 1e-12) {
        errno = ERANGE;
        return -1;
    }
    /* 三项反射误差模型 m=Ed+Er*Γ/(1-Es*Γ)，S21 使用直通跟踪误差。 */
    raw_s11 = error->directivity +
              error->reflection_tracking * ideal_s11 /
              (1.0 - error->source_match * ideal_s11);
    {
        double tracking_ripple = 1.0 +
            0.025 * sin((frequency_hz - 1.40e9) / 6.5e6);
        double drift = fabs(error->variation_phase) < 1e-12 ? 1.0 :
            1.0 + 0.002 * sin((frequency_hz - 1.40e9) / 9.0e5 +
                              error->variation_phase);
        raw_s21 = error->transmission_tracking * tracking_ripple * drift * ideal_s21;
    }

    frame->frequency_hz = frequency_hz;
    frame->sample_count = sample_count;
    for (index = 0; index < sample_count; ++index) {
        double angle = 2.0 * PI * SIMULATOR_TONE_CYCLES *
                       (double)index / (double)sample_count;
        double complex incident = cos(angle) + I * sin(angle);
        frame->samples[index].incident = incident;
        frame->samples[index].reflected = incident * raw_s11;
        frame->samples[index].transmitted = incident * raw_s21;
    }
    return 0;
}
