#define _POSIX_C_SOURCE 200809L
/*
 * Linux 测量服务：TCP 主线程只分发命令，工作线程消费 IQ，采集线程生产 IQ。
 * v1 继续支持原有单次 CSV/device 测量；v2 在同一线程骨架上增加模拟扫频、
 * 四步校准、分页 trace 和应用结果，GUI 断开不会终止后台任务。
 */
#include "application.h"
#include "calibration.h"
#include "config.h"
#include "csv_source.h"
#include "device_source.h"
#include "log.h"
#include "protocol.h"
#include "ring_queue.h"
#include "sweep.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CAL_LOAD  (1U << 0)
#define CAL_OPEN  (1U << 1)
#define CAL_SHORT (1U << 2)
#define CAL_THRU  (1U << 3)
#define CAL_ALL   (CAL_LOAD | CAL_OPEN | CAL_SHORT | CAL_THRU)
#define TRACE_PAGE_MAX 32U

enum job_kind {
    JOB_LEGACY = 0,
    JOB_SWEEP,
    JOB_CAL_LOAD,
    JOB_CAL_OPEN,
    JOB_CAL_SHORT,
    JOB_CAL_THRU,
};

struct measurement {
    struct app_config config;
    struct sweep_config sweep;
    struct simulator_error simulator_error;
    enum grain_type grain;

    struct ring_queue queue;
    pthread_mutex_t lock;
    pthread_t worker;
    enum sparam_state state;
    enum job_kind active_job;
    int stop_requested;
    int worker_active;
    int worker_joinable;
    int queue_ready;
    size_t progress;
    size_t total_points;
    struct sparam_result last_result;
    char error[160];

    struct sparam_trace cal_load;
    struct sparam_trace cal_open;
    struct sparam_trace cal_short;
    struct sparam_trace cal_thru;
    unsigned calibration_mask;
    struct calibration_table calibration;
    struct sparam_trace raw_trace;
    struct sparam_trace calibrated_trace;
    struct resonance_result resonance;
    double moisture_percent;
    int resonance_valid;
    int moisture_valid;
};

struct producer_context {
    struct measurement *measurement;
    struct app_config app_config;
    struct sweep_config sweep;
    enum job_kind job;
    struct simulator_error simulator_error;
    int status;
};

static volatile sig_atomic_t shutdown_requested;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    shutdown_requested = 1;
}

static const char *job_name(enum job_kind job)
{
    static const char *names[] = {
        "legacy", "sweep", "cal_load", "cal_open", "cal_short", "cal_thru"
    };
    return job >= JOB_LEGACY && job <= JOB_CAL_THRU ? names[job] : "unknown";
}

static enum simulator_scenario job_scenario(enum job_kind job,
                                             enum simulator_scenario configured)
{
    if (job == JOB_CAL_LOAD) return SCENARIO_LOAD;
    if (job == JOB_CAL_OPEN) return SCENARIO_OPEN;
    if (job == JOB_CAL_SHORT) return SCENARIO_SHORT;
    if (job == JOB_CAL_THRU) return SCENARIO_THRU;
    return configured;
}

static int should_stop(struct measurement *measurement)
{
    int stop;
    pthread_mutex_lock(&measurement->lock);
    stop = measurement->stop_requested;
    pthread_mutex_unlock(&measurement->lock);
    return stop || shutdown_requested;
}

static void clear_application(struct measurement *m)
{
    trace_free(&m->raw_trace);
    trace_free(&m->calibrated_trace);
    m->resonance_valid = 0;
    m->moisture_valid = 0;
}

static void clear_calibration(struct measurement *m)
{
    trace_free(&m->cal_load);
    trace_free(&m->cal_open);
    trace_free(&m->cal_short);
    trace_free(&m->cal_thru);
    calibration_free(&m->calibration);
    m->calibration_mask = 0;
    clear_application(m);
}

static void move_trace(struct sparam_trace *destination,
                       struct sparam_trace *source)
{
    trace_free(destination);
    *destination = *source;
    memset(source, 0, sizeof(*source));
}

static void *produce_frames(void *argument)
{
    struct producer_context *context = argument;
    struct measurement *m = context->measurement;
    struct sparam_frame frame;
    int status = 0;

    if (context->job == JOB_LEGACY && context->app_config.source == SOURCE_CSV) {
        struct csv_source source = {0};
        if (csv_source_open(&source, context->app_config.input_path) == -1)
            status = -1;
        while (!status && !should_stop(m) &&
               (status = csv_source_next(&source, &frame)) > 0) {
            if (ring_queue_push(&m->queue, &frame) == -1) {
                status = -1;
                break;
            }
        }
        if (status > 0)
            status = 0;
        csv_source_close(&source);
    } else if (context->job == JOB_LEGACY) {
        struct device_source source = { .fd = -1 };
        if (device_source_open(&source, context->app_config.device_path) == -1 ||
            device_source_start(&source) == -1)
            status = -1;
        if (!status && !should_stop(m)) {
            status = device_source_next(&source, &frame);
            if (status > 0)
                status = ring_queue_push(&m->queue, &frame);
        }
        device_source_close(&source);
    } else {
        size_t point_count, index;
        enum simulator_scenario scenario =
            job_scenario(context->job, context->sweep.scenario);
        if (sweep_config_validate(&context->sweep, &point_count))
            status = -1;
        for (index = 0; !status && index < point_count && !should_stop(m); ++index) {
            double frequency = context->sweep.start_hz +
                               context->sweep.step_hz * (double)index;
            if (simulator_generate_frame(scenario, frequency,
                                         context->sweep.sample_count,
                                         &context->simulator_error, &frame) ||
                ring_queue_push(&m->queue, &frame))
                status = -1;
        }
    }
    context->status = status;
    ring_queue_close(&m->queue);
    return NULL;
}

static int store_finished_trace(struct measurement *m, enum job_kind job,
                                struct sparam_trace *trace)
{
    int status = 0;
    if (job == JOB_CAL_LOAD) {
        move_trace(&m->cal_load, trace);
        m->calibration_mask |= CAL_LOAD;
    } else if (job == JOB_CAL_OPEN) {
        move_trace(&m->cal_open, trace);
        m->calibration_mask |= CAL_OPEN;
    } else if (job == JOB_CAL_SHORT) {
        move_trace(&m->cal_short, trace);
        m->calibration_mask |= CAL_SHORT;
    } else if (job == JOB_CAL_THRU) {
        move_trace(&m->cal_thru, trace);
        m->calibration_mask |= CAL_THRU;
        if (m->calibration_mask == CAL_ALL &&
            calibration_build(&m->cal_load, &m->cal_open, &m->cal_short,
                              &m->cal_thru, &m->calibration))
            status = -1;
    } else if (job == JOB_SWEEP) {
        struct sparam_trace calibrated = {0};
        struct resonance_result resonance;
        double moisture = 0.0;
        move_trace(&m->raw_trace, trace);
        trace_free(&m->calibrated_trace);
        m->resonance_valid = 0;
        m->moisture_valid = 0;
        if (m->calibration.valid) {
            if (calibration_apply(&m->calibration, &m->raw_trace, &calibrated))
                return -1;
            move_trace(&m->calibrated_trace, &calibrated);
            if (!resonance_analyze(&m->calibrated_trace, &resonance)) {
                m->resonance = resonance;
                m->resonance_valid = 1;
                if (m->grain != GRAIN_NONE &&
                    !moisture_calculate(m->grain, resonance.f0_hz,
                                        resonance.q, &moisture)) {
                    m->moisture_percent = moisture;
                    m->moisture_valid = 1;
                }
            }
        }
        /* 导出失败不丢弃内存结果；常见原因只是运行目录没有 results。 */
        if (trace_write_csv("results/raw_trace.csv", &m->raw_trace))
            LOGW("trace", "cannot write results/raw_trace.csv: %s", strerror(errno));
        if (m->calibrated_trace.points &&
            trace_write_csv("results/calibrated_trace.csv", &m->calibrated_trace))
            LOGW("trace", "cannot write results/calibrated_trace.csv: %s", strerror(errno));
    }
    return status;
}

static int run_measurement(struct measurement *m)
{
    struct producer_context producer;
    struct sparam_trace local_trace = {0};
    struct sparam_frame frame;
    pthread_t producer_thread;
    FILE *output = NULL;
    enum job_kind job;
    size_t point_count = 0, trace_index = 0;
    int pop_status, status = 0, stopped;

    pthread_mutex_lock(&m->lock);
    memset(&producer, 0, sizeof(producer));
    producer.measurement = m;
    producer.app_config = m->config;
    producer.sweep = m->sweep;
    producer.job = m->active_job;
    producer.simulator_error = m->simulator_error;
    job = m->active_job;
    if (job == JOB_SWEEP) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        producer.simulator_error.variation_phase =
            fmod((double)now.tv_sec * 0.731 + (double)now.tv_nsec / 1e9 * 6.283185307,
                 6.283185307);
    }
    pthread_mutex_unlock(&m->lock);

    if (job != JOB_LEGACY) {
        producer.sweep.scenario = job_scenario(job, producer.sweep.scenario);
        if (sweep_config_validate(&producer.sweep, &point_count) ||
            trace_allocate(&local_trace, &producer.sweep, point_count))
            return -1;
    }
    if (ring_queue_init(&m->queue, m->config.queue_length) == -1) {
        trace_free(&local_trace);
        return -1;
    }
    pthread_mutex_lock(&m->lock);
    m->queue_ready = 1;
    m->progress = 0;
    m->total_points = point_count;
    pthread_mutex_unlock(&m->lock);

    if (job == JOB_LEGACY && csv_result_open(&output, m->config.output_path) == -1)
        status = -1;
    if (!status && pthread_create(&producer_thread, NULL, produce_frames, &producer))
        status = -1;
    if (status) {
        if (output) fclose(output);
        pthread_mutex_lock(&m->lock); m->queue_ready = 0; pthread_mutex_unlock(&m->lock);
        ring_queue_destroy(&m->queue);
        trace_free(&local_trace);
        return -1;
    }

    while ((pop_status = ring_queue_pop(&m->queue, &frame)) > 0) {
        struct sparam_result result;
        if (sparam_calculate(&frame, producer.app_config.algorithm, &result)) {
            status = -1;
            break;
        }
        if (job == JOB_LEGACY) {
            if (csv_result_write(output, &result)) {
                status = -1;
                break;
            }
        } else {
            if (trace_index >= local_trace.point_count) {
                errno = EOVERFLOW;
                status = -1;
                break;
            }
            local_trace.points[trace_index].frequency_hz = result.frequency_hz;
            local_trace.points[trace_index].s11 = result.s11;
            local_trace.points[trace_index].s21 = result.s21;
            trace_index++;
        }
        pthread_mutex_lock(&m->lock);
        m->last_result = result;
        m->progress = trace_index;
        pthread_mutex_unlock(&m->lock);
    }
    if (status)
        ring_queue_close(&m->queue);
    pthread_join(producer_thread, NULL);
    pthread_mutex_lock(&m->lock);
    stopped = m->stop_requested || shutdown_requested;
    pthread_mutex_unlock(&m->lock);
    if (producer.status && !stopped)
        status = -1;
    if (job != JOB_LEGACY && !stopped && trace_index != point_count) {
        errno = EIO;
        status = -1;
    }
    if (output)
        fclose(output);
    pthread_mutex_lock(&m->lock); m->queue_ready = 0; pthread_mutex_unlock(&m->lock);
    ring_queue_destroy(&m->queue);

    if (!status && !stopped && job != JOB_LEGACY) {
        pthread_mutex_lock(&m->lock);
        status = store_finished_trace(m, job, &local_trace);
        pthread_mutex_unlock(&m->lock);
    }
    trace_free(&local_trace);
    return status;
}

static void *measurement_worker(void *argument)
{
    struct measurement *m = argument;
    int status = run_measurement(m);
    int saved_errno = errno;
    pthread_mutex_lock(&m->lock);
    m->state = status == 0 ? SPARAM_DONE : SPARAM_ERROR;
    if (status)
        snprintf(m->error, sizeof(m->error), "%s", strerror(saved_errno));
    m->worker_active = 0;
    pthread_mutex_unlock(&m->lock);
    return NULL;
}

static int start_job(struct measurement *m, enum job_kind job)
{
    pthread_t previous;
    int join_previous = 0;
    pthread_mutex_lock(&m->lock);
    if (m->worker_active || m->state == SPARAM_RUNNING) {
        pthread_mutex_unlock(&m->lock);
        errno = EBUSY;
        return -1;
    }
    if (m->worker_joinable) {
        previous = m->worker;
        m->worker_joinable = 0;
        join_previous = 1;
    }
    pthread_mutex_unlock(&m->lock);
    if (join_previous)
        pthread_join(previous, NULL);

    pthread_mutex_lock(&m->lock);
    m->active_job = job;
    m->state = SPARAM_RUNNING;
    m->stop_requested = 0;
    m->worker_active = 1;
    m->progress = 0;
    m->total_points = 0;
    m->error[0] = '\0';
    if (pthread_create(&m->worker, NULL, measurement_worker, m)) {
        m->worker_active = 0;
        m->state = SPARAM_ERROR;
        pthread_mutex_unlock(&m->lock);
        return -1;
    }
    m->worker_joinable = 1;
    pthread_mutex_unlock(&m->lock);
    return 0;
}

static int get_parameter(const char *payload, const char *key,
                         char *value, size_t capacity)
{
    size_t key_length = strlen(key);
    const char *cursor = payload;
    if (!payload || !key || !value || !capacity)
        return -1;
    while (*cursor) {
        const char *end = strchr(cursor, '&');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length > key_length + 1U && !strncmp(cursor, key, key_length) &&
            cursor[key_length] == '=') {
            size_t value_length = length - key_length - 1U;
            if (value_length >= capacity)
                return -1;
            memcpy(value, cursor + key_length + 1U, value_length);
            value[value_length] = '\0';
            return 0;
        }
        if (!end)
            break;
        cursor = end + 1;
    }
    return -1;
}

static int parse_double_parameter(const char *payload, const char *key,
                                  double *result)
{
    char value[64], *end;
    double parsed;
    if (get_parameter(payload, key, value, sizeof(value)))
        return -1;
    parsed = strtod(value, &end);
    if (*end || !isfinite(parsed))
        return -1;
    *result = parsed;
    return 0;
}

static int parse_size_parameter(const char *payload, const char *key,
                                size_t *result)
{
    char value[64], *end;
    unsigned long parsed;
    if (get_parameter(payload, key, value, sizeof(value)))
        return -1;
    parsed = strtoul(value, &end, 10);
    if (*end)
        return -1;
    *result = (size_t)parsed;
    return 0;
}

static int set_sweep_config(struct measurement *m, const char *payload)
{
    struct sweep_config config;
    char scenario[64], samples[64];
    size_t ignored;
    pthread_mutex_lock(&m->lock);
    if (m->state == SPARAM_RUNNING) {
        pthread_mutex_unlock(&m->lock);
        errno = EBUSY;
        return -1;
    }
    config = m->sweep;
    pthread_mutex_unlock(&m->lock);
    if (parse_double_parameter(payload, "start_hz", &config.start_hz) ||
        parse_double_parameter(payload, "stop_hz", &config.stop_hz) ||
        parse_double_parameter(payload, "step_hz", &config.step_hz) ||
        get_parameter(payload, "scenario", scenario, sizeof(scenario)) ||
        simulator_parse_scenario(scenario, &config.scenario)) {
        errno = EINVAL;
        return -1;
    }
    if (!get_parameter(payload, "samples", samples, sizeof(samples))) {
        if (parse_size_parameter(payload, "samples", &config.sample_count)) {
            errno = EINVAL;
            return -1;
        }
    }
    if (sweep_config_validate(&config, &ignored))
        return -1;
    pthread_mutex_lock(&m->lock);
    /* 只有频率网格或帧规格变化才使校准表失效；切换 DUT 场景仍复用同一校准。 */
    if (m->sweep.start_hz != config.start_hz ||
        m->sweep.stop_hz != config.stop_hz ||
        m->sweep.step_hz != config.step_hz ||
        m->sweep.sample_count != config.sample_count)
        clear_calibration(m);
    else
        clear_application(m);
    m->sweep = config;
    pthread_mutex_unlock(&m->lock);
    return 0;
}

static const char *next_calibration_step(unsigned mask)
{
    if (!(mask & CAL_LOAD)) return "load";
    if (!(mask & CAL_OPEN)) return "open";
    if (!(mask & CAL_SHORT)) return "short";
    if (!(mask & CAL_THRU)) return "thru";
    return "done";
}

static int start_calibration_step(struct measurement *m, const char *payload)
{
    char standard[32];
    enum job_kind job;
    unsigned expected_mask;
    if (get_parameter(payload, "standard", standard, sizeof(standard))) {
        if (strlen(payload) >= sizeof(standard)) {
            errno = EINVAL;
            return -1;
        }
        strcpy(standard, payload);
    }
    if (!strcmp(standard, "load")) { job = JOB_CAL_LOAD; expected_mask = 0; }
    else if (!strcmp(standard, "open")) { job = JOB_CAL_OPEN; expected_mask = CAL_LOAD; }
    else if (!strcmp(standard, "short")) { job = JOB_CAL_SHORT; expected_mask = CAL_LOAD | CAL_OPEN; }
    else if (!strcmp(standard, "thru")) { job = JOB_CAL_THRU; expected_mask = CAL_LOAD | CAL_OPEN | CAL_SHORT; }
    else { errno = EINVAL; return -1; }
    pthread_mutex_lock(&m->lock);
    if (m->calibration_mask != expected_mask || m->state == SPARAM_RUNNING) {
        pthread_mutex_unlock(&m->lock);
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_unlock(&m->lock);
    return start_job(m, job);
}

static void status_text(struct measurement *m, char *buffer, size_t capacity)
{
    static const char *names[] = { "IDLE", "RUNNING", "DONE", "ERROR" };
    pthread_mutex_lock(&m->lock);
    snprintf(buffer, capacity,
             "state=%s algorithm=%s job=%s progress=%zu total=%zu scenario=%s grain=%s calibrated=%d error=%s",
             names[m->state], sparam_algorithm_name(m->config.algorithm),
             job_name(m->active_job), m->progress, m->total_points,
             simulator_scenario_name(m->sweep.scenario), grain_type_name(m->grain),
             m->calibration.valid, m->error);
    pthread_mutex_unlock(&m->lock);
}

static void trace_page_text(struct measurement *m, const char *payload,
                            char *response, size_t capacity)
{
    char representation[32], channel[16];
    size_t offset = 0, count = 20, index, used;
    const struct sparam_trace *trace;
    int use_s11;
    if (get_parameter(payload, "representation", representation,
                      sizeof(representation)) ||
        get_parameter(payload, "channel", channel, sizeof(channel)) ||
        parse_size_parameter(payload, "offset", &offset)) {
        snprintf(response, capacity, "error=invalid_parameter");
        return;
    }
    (void)parse_size_parameter(payload, "count", &count);
    if (!count || count > TRACE_PAGE_MAX) count = TRACE_PAGE_MAX;
    if (!strcmp(representation, "raw")) trace = &m->raw_trace;
    else if (!strcmp(representation, "calibrated")) trace = &m->calibrated_trace;
    else { snprintf(response, capacity, "error=invalid_parameter"); return; }
    if (!strcmp(channel, "s11")) use_s11 = 1;
    else if (!strcmp(channel, "s21")) use_s11 = 0;
    else { snprintf(response, capacity, "error=invalid_parameter"); return; }
    if (!trace->points) { snprintf(response, capacity, "error=no_result"); return; }
    if (offset > trace->point_count) {
        snprintf(response, capacity, "error=invalid_parameter");
        return;
    }
    if (count > trace->point_count - offset)
        count = trace->point_count - offset;
    used = (size_t)snprintf(response, capacity,
                            "total=%zu&offset=%zu&count=%zu&representation=%s&channel=%s\n",
                            trace->point_count, offset, count, representation, channel);
    for (index = 0; index < count && used < capacity; ++index) {
        const struct trace_point *point = &trace->points[offset + index];
        double complex value = use_s11 ? point->s11 : point->s21;
        double magnitude = cabs(value);
        double db = magnitude > 1e-15 ? 20.0 * log10(magnitude) : -300.0;
        double phase = carg(value) * 180.0 / 3.14159265358979323846;
        int written = snprintf(response + used, capacity - used,
                               "%.0f,%.12g,%.12g,%.9g,%.9g\n",
                               point->frequency_hz, creal(value), cimag(value), db, phase);
        if (written < 0 || (size_t)written >= capacity - used)
            break;
        used += (size_t)written;
    }
}

static int is_v2_command(uint16_t command)
{
    return command >= CMD_SET_SWEEP && command <= CMD_GET_APPLICATION_RESULT;
}

static int handle_client(int client, struct measurement *m)
{
    char payload[SPARAM_MAX_PAYLOAD + 1];
    struct protocol_header header;
    while (!shutdown_requested) {
        char response[SPARAM_MAX_PAYLOAD + 1];
        int status = protocol_receive(client, &header, payload, SPARAM_MAX_PAYLOAD);
        if (status <= 0)
            return status;
        payload[header.payload_length] = '\0';
        if (is_v2_command(header.command) && header.version != SPARAM_VERSION_V2) {
            snprintf(response, sizeof(response), "error=version_required");
        } else if (header.command == CMD_START) {
            snprintf(response, sizeof(response), "%s",
                     start_job(m, JOB_LEGACY) ? "error=busy" : "ok");
        } else if (header.command == CMD_STOP) {
            int queue_ready;
            pthread_mutex_lock(&m->lock);
            queue_ready = m->state == SPARAM_RUNNING && m->queue_ready;
            m->stop_requested = 1;
            if (queue_ready)
                ring_queue_close(&m->queue);
            pthread_mutex_unlock(&m->lock);
            snprintf(response, sizeof(response), "ok");
        } else if (header.command == CMD_STATUS) {
            status_text(m, response, sizeof(response));
        } else if (header.command == CMD_SET_ALGORITHM) {
            enum sparam_algorithm algorithm;
            pthread_mutex_lock(&m->lock);
            if (m->state == SPARAM_RUNNING || sparam_parse_algorithm(payload, &algorithm))
                snprintf(response, sizeof(response), "error=invalid_or_busy");
            else {
                if (m->config.algorithm != algorithm)
                    clear_calibration(m);
                m->config.algorithm = algorithm;
                snprintf(response, sizeof(response), "ok");
            }
            pthread_mutex_unlock(&m->lock);
        } else if (header.command == CMD_GET_RESULT) {
            pthread_mutex_lock(&m->lock);
            snprintf(response, sizeof(response),
                     "frequency_hz=%.0f s11_db=%.6f s11_phase=%.6f s21_db=%.6f s21_phase=%.6f elapsed_us=%llu",
                     m->last_result.frequency_hz, m->last_result.s11_mag_db,
                     m->last_result.s11_phase_deg, m->last_result.s21_mag_db,
                     m->last_result.s21_phase_deg,
                     (unsigned long long)m->last_result.elapsed_us);
            pthread_mutex_unlock(&m->lock);
        } else if (header.command == CMD_SET_SWEEP) {
            snprintf(response, sizeof(response), "%s",
                     set_sweep_config(m, payload) ? "error=invalid_or_busy" : "ok");
        } else if (header.command == CMD_CALIBRATE_BEGIN) {
            pthread_mutex_lock(&m->lock);
            if (m->state == SPARAM_RUNNING)
                snprintf(response, sizeof(response), "error=busy");
            else {
                clear_calibration(m);
                snprintf(response, sizeof(response), "ok&next=load");
            }
            pthread_mutex_unlock(&m->lock);
        } else if (header.command == CMD_CALIBRATE_STEP) {
            snprintf(response, sizeof(response), "%s",
                     start_calibration_step(m, payload) ? "error=invalid_order_or_busy" : "ok");
        } else if (header.command == CMD_CALIBRATE_STATUS) {
            pthread_mutex_lock(&m->lock);
            snprintf(response, sizeof(response), "mask=%u&ready=%d&next=%s",
                     m->calibration_mask, m->calibration.valid,
                     next_calibration_step(m->calibration_mask));
            pthread_mutex_unlock(&m->lock);
        } else if (header.command == CMD_SET_GRAIN) {
            enum grain_type grain;
            pthread_mutex_lock(&m->lock);
            if (m->state == SPARAM_RUNNING || grain_parse(payload, &grain))
                snprintf(response, sizeof(response), "error=invalid_or_busy");
            else {
                m->grain = grain;
                m->moisture_valid = 0;
                if (grain != GRAIN_NONE && m->resonance_valid &&
                    !moisture_calculate(grain, m->resonance.f0_hz,
                                        m->resonance.q, &m->moisture_percent))
                    m->moisture_valid = 1;
                snprintf(response, sizeof(response), "ok");
            }
            pthread_mutex_unlock(&m->lock);
        } else if (header.command == CMD_START_SWEEP) {
            snprintf(response, sizeof(response), "%s",
                     start_job(m, JOB_SWEEP) ? "error=busy" : "ok");
        } else if (header.command == CMD_GET_TRACE) {
            pthread_mutex_lock(&m->lock);
            trace_page_text(m, payload, response, sizeof(response));
            pthread_mutex_unlock(&m->lock);
        } else if (header.command == CMD_GET_APPLICATION_RESULT) {
            pthread_mutex_lock(&m->lock);
            if (!m->calibration.valid)
                snprintf(response, sizeof(response), "error=not_calibrated");
            else if (!m->resonance_valid)
                snprintf(response, sizeof(response), "error=no_resonance");
            else if (m->grain != GRAIN_NONE && !m->moisture_valid)
                snprintf(response, sizeof(response), "error=out_of_model_range");
            else if (m->grain == GRAIN_NONE)
                snprintf(response, sizeof(response),
                         "f0_hz=%.3f&f1_hz=%.3f&f2_hz=%.3f&q=%.6f&grain=none",
                         m->resonance.f0_hz, m->resonance.f1_hz,
                         m->resonance.f2_hz, m->resonance.q);
            else
                snprintf(response, sizeof(response),
                         "f0_hz=%.3f&f1_hz=%.3f&f2_hz=%.3f&q=%.6f&grain=%s&moisture=%.6f",
                         m->resonance.f0_hz, m->resonance.f1_hz,
                         m->resonance.f2_hz, m->resonance.q,
                         grain_type_name(m->grain), m->moisture_percent);
            pthread_mutex_unlock(&m->lock);
        } else {
            snprintf(response, sizeof(response), "error=unknown_command");
        }
        if (protocol_send_version(client, header.version, CMD_RESPONSE, response,
                                  (uint32_t)strlen(response)))
            return -1;
    }
    return 0;
}

static int run_server(struct measurement *m)
{
    int server = -1, option = 1;
    struct sockaddr_in address = {0};
    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0)
        return -1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m->config.port);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) || listen(server, 1)) {
        close(server);
        return -1;
    }
    LOGI("network", "listening on port %u", m->config.port);
    while (!shutdown_requested) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            break;
        }
        handle_client(client, m);
        close(client);
    }
    close(server);
    return 0;
}

int main(int argc, char **argv)
{
    const char *config_path = "config/sparam.conf";
    int once = 0, i, status;
    unsigned long error_line = 0;
    struct measurement measurement;
    struct sigaction action;

    memset(&measurement, 0, sizeof(measurement));
    config_defaults(&measurement.config);
    measurement.sweep.start_hz = 1.45e9;
    measurement.sweep.stop_hz = 1.55e9;
    measurement.sweep.step_hz = 1.0e6;
    measurement.sweep.sample_count = 256;
    measurement.sweep.scenario = SCENARIO_RICE_DEMO;
    measurement.grain = GRAIN_NONE;
    simulator_default_error(&measurement.simulator_error);
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--once")) once = 1;
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) config_path = argv[++i];
        else { fprintf(stderr, "usage: %s [--config FILE] [--once]\n", argv[0]); return 2; }
    }
    if (config_load(&measurement.config, config_path, &error_line)) {
        int config_errno = errno;
        if (config_errno == ENOENT)
            LOGW("config", "cannot load %s: using defaults", config_path);
        else {
            LOGE("config", "cannot load %s (line %lu): %s", config_path,
                 error_line, strerror(config_errno));
            return 1;
        }
    }
    log_set_level(measurement.config.log_level);
    pthread_mutex_init(&measurement.lock, NULL);
    measurement.state = SPARAM_IDLE;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    if (once) status = run_measurement(&measurement);
    else status = run_server(&measurement);
    pthread_mutex_lock(&measurement.lock);
    measurement.stop_requested = 1;
    i = measurement.worker_joinable;
    if (measurement.queue_ready)
        ring_queue_close(&measurement.queue);
    pthread_mutex_unlock(&measurement.lock);
    if (i)
        pthread_join(measurement.worker, NULL);
    clear_calibration(&measurement);
    pthread_mutex_destroy(&measurement.lock);
    if (status)
        LOGE("service", "%s", strerror(errno));
    return status ? 1 : 0;
}
