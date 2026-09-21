#ifndef SPARAM_DMA_SLOT_STATE_MODEL_H
#define SPARAM_DMA_SLOT_STATE_MODEL_H

#include <stdint.h>

/* 此模型只验证客户端的槽位所有权，不连接 DMA 硬件或 Linux 内核。 */
#define SPARAM_DMA_MODEL_SLOT_COUNT 3

enum sparam_dma_model_slot_state {
    SPARAM_DMA_MODEL_FREE,
    SPARAM_DMA_MODEL_QUEUED,
    SPARAM_DMA_MODEL_STOPPING,
    SPARAM_DMA_MODEL_READY,
    SPARAM_DMA_MODEL_READING,
};

enum sparam_dma_model_result {
    SPARAM_DMA_MODEL_OK = 0,
    SPARAM_DMA_MODEL_NO_SLOT = -1,
    SPARAM_DMA_MODEL_STALE_CALLBACK = -2,
    SPARAM_DMA_MODEL_NO_READY_RESULT = -3,
    SPARAM_DMA_MODEL_INVALID_TRANSFER = -4,
};

struct sparam_dma_model_slot {
    uint64_t sequence;
    int cookie;
    enum sparam_dma_model_slot_state state;
};

/* 这相当于描述符 callback_param 中保存的“本次提交身份”。 */
struct sparam_dma_model_transfer {
    unsigned int slot_index;
    uint64_t sequence;
    int cookie;
};

struct sparam_dma_model_client {
    struct sparam_dma_model_slot slots[SPARAM_DMA_MODEL_SLOT_COUNT];
    uint64_t next_sequence;
    unsigned int ready_slots;
    unsigned int complete_count;
    unsigned int stale_callback_count;
    unsigned int dma_error_count;
    unsigned int timeout_count;
    unsigned int no_slot_count;
    unsigned int read_fault_count;
};

void sparam_dma_model_init(struct sparam_dma_model_client *client);
int sparam_dma_model_submit(struct sparam_dma_model_client *client, int cookie,
                             struct sparam_dma_model_transfer *transfer);
int sparam_dma_model_complete(struct sparam_dma_model_client *client,
                               const struct sparam_dma_model_transfer *transfer);
int sparam_dma_model_begin_stop(struct sparam_dma_model_client *client,
                                 const struct sparam_dma_model_transfer *transfer);
int sparam_dma_model_finish_stop(struct sparam_dma_model_client *client,
                                  const struct sparam_dma_model_transfer *transfer);
int sparam_dma_model_timeout(struct sparam_dma_model_client *client,
                              const struct sparam_dma_model_transfer *transfer);
int sparam_dma_model_dma_error(struct sparam_dma_model_client *client,
                                const struct sparam_dma_model_transfer *transfer);
/* 先独占一帧，再在锁外模拟 copy_to_user()；避免两个 reader 同时取同一 READY 槽位。 */
int sparam_dma_model_begin_read(struct sparam_dma_model_client *client,
                                unsigned int *slot_index, uint64_t *sequence);
/* copy_succeeded 为 1 时释放槽位；为 0 时把帧退回 READY 以便用户态重试。 */
int sparam_dma_model_finish_read(struct sparam_dma_model_client *client,
                                 unsigned int slot_index, int copy_succeeded);
int sparam_dma_model_poll_readable(const struct sparam_dma_model_client *client);

#endif
