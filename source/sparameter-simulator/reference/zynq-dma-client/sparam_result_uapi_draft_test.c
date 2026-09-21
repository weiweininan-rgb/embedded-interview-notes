#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "sparam_result_uapi_draft.h"

/* 该测试只锁定教学草案的主机 ABI 布局与字段语义，不验证 PL 或 DMA。 */
int main(void)
{
    struct sparam_result_frame_v1 frame = {0};
    struct sparam_result_status_v1 status = {0};

    _Static_assert(sizeof(struct sparam_result_complex_accum_v1) == 16,
                   "complex accumulator must contain two signed 64-bit values");
    _Static_assert(sizeof(struct sparam_result_frame_header_v1) == 48,
                   "common result header layout changed; review the ABI deliberately");
    _Static_assert(offsetof(struct sparam_result_frame_header_v1, sequence) == 16,
                   "frame header must leave sequence 8-byte aligned");
    _Static_assert(sizeof(struct sparam_result_frame_v1) == 120,
                   "draft frame layout changed; review the ABI deliberately");
    _Static_assert(sizeof(struct sparam_result_status_v1) == 80,
                   "draft status layout changed; review the ABI deliberately");

    frame.header.magic = SPARAM_RESULT_FRAME_MAGIC;
    frame.header.abi_version = SPARAM_RESULT_ABI_VERSION;
    frame.header.header_bytes = sizeof(frame.header);
    frame.header.frame_bytes = sizeof(frame.header) + sizeof(frame.payload.correlation);
    frame.header.algorithm = SPARAM_RESULT_ALGORITHM_CORRELATION;
    frame.header.sequence = 18;
    frame.header.frequency_hz = 915000000ULL;
    frame.header.sample_count = 1024;
    frame.header.status = SPARAM_RESULT_STATUS_VALID;
    frame.payload.correlation.corr_scale_exp2 = -20;
    frame.payload.correlation.paa_accum = 4000000ULL;
    frame.payload.correlation.pba_accum.real = 1000000;
    frame.payload.correlation.pba_accum.imag = 500000;

    assert(frame.header.magic == SPARAM_RESULT_FRAME_MAGIC);
    assert(frame.header.frame_bytes == 96);
    assert(frame.header.sequence == 18);
    assert(frame.header.status & SPARAM_RESULT_STATUS_VALID);
    assert(frame.payload.correlation.paa_accum != 0);

    /* 同一固定大小容器也可承载 FFT 的一个 bin，用户态按 algorithm 分支解释 union。 */
    frame.header.algorithm = SPARAM_RESULT_ALGORITHM_FFT_BIN;
    frame.header.frame_bytes = sizeof(frame.header) + sizeof(frame.payload.fft_bin);
    frame.payload.fft_bin.fft_size = 1024;
    frame.payload.fft_bin.bin_index = 64;
    frame.payload.fft_bin.window = SPARAM_RESULT_FFT_WINDOW_HANN;
    frame.payload.fft_bin.average_count = 8;
    frame.payload.fft_bin.spectrum_scale_exp2 = -18;
    frame.payload.fft_bin.incident_bin.real = 2000000;
    frame.payload.fft_bin.reflected_bin.real = 500000;

    assert(frame.header.frame_bytes == sizeof(struct sparam_result_frame_v1));
    assert(frame.payload.fft_bin.fft_size == 1024);
    assert(frame.payload.fft_bin.bin_index == 64);
    assert(frame.payload.fft_bin.window == SPARAM_RESULT_FFT_WINDOW_HANN);
    assert(frame.payload.fft_bin.average_count == 8);

    status.state = SPARAM_RESULT_STATE_ARMED;
    status.ready_slots = 1;
    status.queued_slots = 3;
    status.no_slot_count = 2;
    status.read_fault_count = 1;
    assert(status.ready_slots + status.queued_slots == 4);
    assert(status.no_slot_count == 2);
    assert(status.read_fault_count == 1);
    assert(SPARAM_RESULT_IOC_ARM_RX != SPARAM_RESULT_IOC_STOP_RX);

    puts("result UAPI draft: PASS");
    return 0;
}
