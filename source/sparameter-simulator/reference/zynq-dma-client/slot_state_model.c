#include "slot_state_model.h"

void sparam_dma_model_init(struct sparam_dma_model_client *client)
{
    unsigned int index;

    client->next_sequence = 0;
    client->ready_slots = 0;
    client->complete_count = 0;
    client->stale_callback_count = 0;
    client->dma_error_count = 0;
    client->timeout_count = 0;
    client->no_slot_count = 0;
    client->read_fault_count = 0;
    /* 测试开始时没有已提交描述符，也没有可读结果。 */
    for (index = 0; index < SPARAM_DMA_MODEL_SLOT_COUNT; index++) {
        client->slots[index].sequence = 0;
        client->slots[index].cookie = 0;
        client->slots[index].state = SPARAM_DMA_MODEL_FREE;
    }
}

int sparam_dma_model_submit(struct sparam_dma_model_client *client, int cookie,
                             struct sparam_dma_model_transfer *transfer)
{
    unsigned int index;

    for (index = 0; index < SPARAM_DMA_MODEL_SLOT_COUNT; index++) {
        struct sparam_dma_model_slot *slot = &client->slots[index];

        if (slot->state != SPARAM_DMA_MODEL_FREE)
            continue;

        /* 只允许 FREE → QUEUED；READY 表示用户态尚未读取，绝不覆盖。 */
        slot->state = SPARAM_DMA_MODEL_QUEUED;
        slot->sequence = ++client->next_sequence;
        slot->cookie = cookie;
        /* 保存提交瞬间的身份，模拟 DMA 描述符的 callback_param。 */
        transfer->slot_index = index;
        transfer->sequence = slot->sequence;
        transfer->cookie = cookie;
        return SPARAM_DMA_MODEL_OK;
    }
    client->no_slot_count++;
    return SPARAM_DMA_MODEL_NO_SLOT;
}

int sparam_dma_model_complete(struct sparam_dma_model_client *client,
                               const struct sparam_dma_model_transfer *transfer)
{
    struct sparam_dma_model_slot *slot;

    if (transfer->slot_index >= SPARAM_DMA_MODEL_SLOT_COUNT)
        goto stale;

    slot = &client->slots[transfer->slot_index];
    /* 如果槽位已被读走并复用，旧 transfer 的 sequence/cookie 将不再匹配。 */
    if (slot->state != SPARAM_DMA_MODEL_QUEUED ||
        slot->sequence != transfer->sequence || slot->cookie != transfer->cookie)
        goto stale;

    slot->state = SPARAM_DMA_MODEL_READY;
    client->ready_slots++;
    client->complete_count++;
    return SPARAM_DMA_MODEL_OK;

stale:
    client->stale_callback_count++;
    return SPARAM_DMA_MODEL_STALE_CALLBACK;
}

int sparam_dma_model_begin_stop(struct sparam_dma_model_client *client,
                                 const struct sparam_dma_model_transfer *transfer)
{
    struct sparam_dma_model_slot *slot;

    if (transfer->slot_index >= SPARAM_DMA_MODEL_SLOT_COUNT)
        return SPARAM_DMA_MODEL_INVALID_TRANSFER;

    slot = &client->slots[transfer->slot_index];
    if (slot->state != SPARAM_DMA_MODEL_QUEUED ||
        slot->sequence != transfer->sequence || slot->cookie != transfer->cookie)
        return SPARAM_DMA_MODEL_INVALID_TRANSFER;

    /* 模拟超时：从此刻起拒绝 CPU 读取和新 DMA 复用，等待硬件终止确认。 */
    slot->state = SPARAM_DMA_MODEL_STOPPING;
    return SPARAM_DMA_MODEL_OK;
}

int sparam_dma_model_finish_stop(struct sparam_dma_model_client *client,
                                  const struct sparam_dma_model_transfer *transfer)
{
    struct sparam_dma_model_slot *slot;

    if (transfer->slot_index >= SPARAM_DMA_MODEL_SLOT_COUNT)
        return SPARAM_DMA_MODEL_INVALID_TRANSFER;

    slot = &client->slots[transfer->slot_index];
    if (slot->state != SPARAM_DMA_MODEL_STOPPING ||
        slot->sequence != transfer->sequence || slot->cookie != transfer->cookie)
        return SPARAM_DMA_MODEL_INVALID_TRANSFER;

    /* 对应真实驱动中“目标内核的 DMA 终止/同步回收已经确认完成”之后。 */
    slot->state = SPARAM_DMA_MODEL_FREE;
    return SPARAM_DMA_MODEL_OK;
}

int sparam_dma_model_timeout(struct sparam_dma_model_client *client,
                              const struct sparam_dma_model_transfer *transfer)
{
    int result = sparam_dma_model_begin_stop(client, transfer);

    if (result == SPARAM_DMA_MODEL_OK)
        client->timeout_count++;
    return result;
}

int sparam_dma_model_dma_error(struct sparam_dma_model_client *client,
                                const struct sparam_dma_model_transfer *transfer)
{
    int result = sparam_dma_model_begin_stop(client, transfer);

    if (result == SPARAM_DMA_MODEL_OK)
        client->dma_error_count++;
    return result;
}

int sparam_dma_model_begin_read(struct sparam_dma_model_client *client,
                                unsigned int *slot_index, uint64_t *sequence)
{
    unsigned int index;

    for (index = 0; index < SPARAM_DMA_MODEL_SLOT_COUNT; index++) {
        struct sparam_dma_model_slot *slot = &client->slots[index];

        if (slot->state != SPARAM_DMA_MODEL_READY)
            continue;

        /*
         * 真实驱动在持有 spinlock 时只完成 READY -> READING 的所有权转移，随后解锁。
         * copy_to_user() 可能睡眠，不能放在 spinlock 中；READING 防止第二个 read() 重复取此帧。
         */
        *sequence = slot->sequence;
        *slot_index = index;
        slot->state = SPARAM_DMA_MODEL_READING;
        client->ready_slots--;
        return SPARAM_DMA_MODEL_OK;
    }
    return SPARAM_DMA_MODEL_NO_READY_RESULT;
}

int sparam_dma_model_finish_read(struct sparam_dma_model_client *client,
                                 unsigned int slot_index, int copy_succeeded)
{
    struct sparam_dma_model_slot *slot;

    if (slot_index >= SPARAM_DMA_MODEL_SLOT_COUNT)
        return SPARAM_DMA_MODEL_INVALID_TRANSFER;

    slot = &client->slots[slot_index];
    if (slot->state != SPARAM_DMA_MODEL_READING)
        return SPARAM_DMA_MODEL_INVALID_TRANSFER;

    if (copy_succeeded) {
        /* 用户态已拿到完整帧，此槽位才可被下一次 DMA 提交复用。 */
        slot->state = SPARAM_DMA_MODEL_FREE;
    } else {
        /* 用户指针无效等复制失败不能悄悄丢帧；将它重新发布为 READY。 */
        slot->state = SPARAM_DMA_MODEL_READY;
        client->ready_slots++;
        client->read_fault_count++;
    }
    return SPARAM_DMA_MODEL_OK;
}

int sparam_dma_model_poll_readable(const struct sparam_dma_model_client *client)
{
    return client->ready_slots > 0;
}
