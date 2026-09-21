CC ?= gcc
CPPFLAGS := -Iinclude
CFLAGS := -Wall -Wextra -Werror -std=c11 -O2 -g
LDLIBS := -lm -pthread
BUILD := build

COMMON_SRC := src/algorithm.c src/csv_source.c src/device_source.c src/config.c src/log.c src/ring_queue.c src/protocol.c src/simulator.c src/sweep.c src/calibration.c src/application.c
COMMON_OBJ := $(COMMON_SRC:src/%.c=$(BUILD)/%.o)

.PHONY: all clean test test-e2e sanitize thread-sanitize

all: $(BUILD)/sparam_service $(BUILD)/sparam_client $(BUILD)/sparam_benchmark $(BUILD)/sparam_device_check $(BUILD)/rf_switch_check $(BUILD)/test_algorithms $(BUILD)/test_application $(BUILD)/test_csv $(BUILD)/test_queue $(BUILD)/test_protocol $(BUILD)/test_config $(BUILD)/test_log $(BUILD)/test_rf_switch_uapi $(BUILD)/generate_samples

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/sparam_service: src/service.c $(COMMON_OBJ) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/sparam_client: src/client.c $(BUILD)/protocol.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_algorithms: tests/test_algorithms.c $(BUILD)/algorithm.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_application: tests/test_application.c $(BUILD)/algorithm.o $(BUILD)/simulator.o $(BUILD)/sweep.o $(BUILD)/calibration.o $(BUILD)/application.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_csv: tests/test_csv.c $(BUILD)/csv_source.o $(BUILD)/algorithm.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_queue: tests/test_queue.c $(BUILD)/ring_queue.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_protocol: tests/test_protocol.c $(BUILD)/protocol.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_config: tests/test_config.c $(BUILD)/config.o $(BUILD)/algorithm.o $(BUILD)/log.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_log: tests/test_log.c $(BUILD)/log.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_rf_switch_uapi: tests/test_rf_switch_uapi.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/generate_samples: tools/generate_samples.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/sparam_benchmark: tools/benchmark.c $(BUILD)/algorithm.o | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/sparam_device_check: tools/sparam_device_check.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -lrt -o $@

$(BUILD)/rf_switch_check: tools/rf_switch_check.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ $(LDLIBS) -o $@

data/samples_256.csv: $(BUILD)/generate_samples
	mkdir -p data
	$(BUILD)/generate_samples $@ 256

test: all data/samples_256.csv
	mkdir -p results
	$(BUILD)/test_algorithms
	$(BUILD)/test_application
	$(BUILD)/test_csv
	$(BUILD)/test_queue
	$(BUILD)/test_protocol
	$(BUILD)/test_config
	$(BUILD)/test_log
	$(BUILD)/test_rf_switch_uapi
	$(BUILD)/sparam_service --config config/test.conf --once

sanitize: CFLAGS += -O1 -fsanitize=address,undefined
sanitize: LDLIBS += -fsanitize=address,undefined
sanitize: clean test

thread-sanitize: CFLAGS += -O1 -fsanitize=thread
thread-sanitize: LDLIBS += -fsanitize=thread
thread-sanitize: clean test

test-e2e: all data/samples_256.csv
	mkdir -p results
	SPARAM_BUILD=$(BUILD) python3 tests/test_e2e.py

clean:
	$(RM) -r $(BUILD) results data/samples_256.csv
