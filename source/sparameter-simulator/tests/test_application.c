#include "application.h"
#include "calibration.h"
#include "sweep.h"

#include <assert.h>
#include <complex.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>

#define PI 3.14159265358979323846

static struct sweep_config config_for(enum simulator_scenario scenario)
{
    struct sweep_config config = {
        .start_hz = 1.45e9,
        .stop_hz = 1.55e9,
        .step_hz = 1.0e6,
        .sample_count = 256,
        .scenario = scenario,
    };
    return config;
}

static void expect_complex(double complex actual, double complex expected,
                           double tolerance)
{
    assert(cabs(actual - expected) < tolerance);
}

int main(void)
{
    struct simulator_error error;
    struct sparam_trace load = {0}, open = {0}, short_trace = {0}, thru = {0};
    struct sparam_trace raw = {0}, calibrated = {0}, rice = {0}, varied = {0}, invalid = {0};
    struct calibration_table table = {0};
    struct resonance_result resonance;
    struct sweep_config config;
    double moisture;

    simulator_default_error(&error);
    config = config_for(SCENARIO_LOAD);
    {
        struct sweep_config invalid_config = config;
        size_t point_count;
        invalid_config.stop_hz += 123.0;
        assert(sweep_config_validate(&invalid_config, &point_count) == -1);
    }
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &load) == 0);
    config.scenario = SCENARIO_OPEN;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &open) == 0);
    config.scenario = SCENARIO_SHORT;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &short_trace) == 0);
    config.scenario = SCENARIO_THRU;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &thru) == 0);
    assert(calibration_build(&load, &open, &short_trace, &thru, &table) == 0);

    config.scenario = SCENARIO_ATTENUATOR_6DB;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &raw) == 0);
    assert(calibration_apply(&table, &raw, &calibrated) == 0);
    expect_complex(calibrated.points[50].s11,
                   0.03 * (cos(10.0 * PI / 180.0) +
                           I * sin(10.0 * PI / 180.0)), 1e-9);
    assert(fabs(20.0 * log10(cabs(calibrated.points[50].s21)) + 6.0) < 1e-9);

    config = config_for(SCENARIO_RICE_DEMO);
    config.step_hz = 1.0e5;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &rice) == 0);
    assert(calibration_apply(&table, &rice, &calibrated) == -1); /* 网格不同必须拒绝。 */
    assert(errno == EINVAL);
    calibration_free(&table);
    trace_free(&load); trace_free(&open); trace_free(&short_trace); trace_free(&thru);

    /* 用相同 0.1 MHz 网格重新校准，再验证 f0=1.5 GHz、Q=50。 */
    config.scenario = SCENARIO_LOAD;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &load) == 0);
    config.scenario = SCENARIO_OPEN;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &open) == 0);
    config.scenario = SCENARIO_SHORT;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &short_trace) == 0);
    config.scenario = SCENARIO_THRU;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &thru) == 0);
    assert(calibration_build(&load, &open, &short_trace, &thru, &table) == 0);
    assert(calibration_apply(&table, &rice, &calibrated) == 0);
    assert(resonance_analyze(&calibrated, &resonance) == 0);
    assert(fabs(resonance.f0_hz - 1.5e9) < 1.0);
    assert(fabs(resonance.q - 50.0) < 0.1);
    assert(moisture_calculate(GRAIN_RICE, resonance.f0_hz,
                              resonance.q, &moisture) == 0);
    assert(moisture > 8.0 && moisture < 9.0);
    assert(moisture_calculate(GRAIN_MUNG_BEAN, 1.4338e9, 36.8,
                              &moisture) == 0);
    assert(fabs(moisture - 18.4420) < 1e-3);
    assert(moisture_calculate(GRAIN_RICE, 1.5e9, 500.0, &moisture) == -1);

    /* 运行时相位允许轻微变化，但主峰和 Q 必须仍处在业务场景的合理范围。 */
    error.variation_phase = 1.2;
    config = config_for(SCENARIO_RICE_DEMO);
    config.step_hz = 1.0e5;
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &varied) == 0);
    assert(calibration_apply(&table, &varied, &calibrated) == 0);
    assert(resonance_analyze(&calibrated, &resonance) == 0);
    assert(fabs(resonance.f0_hz - 1.5e9) <= 5.0e5);
    assert(resonance.q > 47.0 && resonance.q < 53.0);
    assert(cabs(varied.points[500].s21 - rice.points[500].s21) > 1e-4);

    config = config_for(SCENARIO_INVALID_TRACE);
    assert(sweep_run(&config, SPARAM_ALG_CORRELATION, &error, &invalid) == 0);
    assert(resonance_analyze(&invalid, &resonance) == -1);

    trace_free(&load); trace_free(&open); trace_free(&short_trace); trace_free(&thru);
    trace_free(&raw); trace_free(&calibrated); trace_free(&rice); trace_free(&varied); trace_free(&invalid);
    calibration_free(&table);
    puts("application tests: PASS");
    return 0;
}
