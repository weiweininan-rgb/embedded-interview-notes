#include <assert.h>
#include <stdio.h>

#include "slot_state_model.h"

int main(void)
{
    struct sparam_dma_model_client client;
    struct sparam_dma_model_transfer first;
    struct sparam_dma_model_transfer second;
    struct sparam_dma_model_transfer third;
    struct sparam_dma_model_transfer reused;
    uint64_t sequence;
    unsigned int read_slot;

    sparam_dma_model_init(&client);

    /* 三个槽位全部提交后，第四笔必须被拒绝，不能覆盖任一已占用槽位。 */
    assert(sparam_dma_model_submit(&client, 101, &first) == SPARAM_DMA_MODEL_OK);
    assert(sparam_dma_model_submit(&client, 102, &second) == SPARAM_DMA_MODEL_OK);
    assert(sparam_dma_model_submit(&client, 103, &third) == SPARAM_DMA_MODEL_OK);
    assert(sparam_dma_model_submit(&client, 104, &reused) == SPARAM_DMA_MODEL_NO_SLOT);
    assert(client.no_slot_count == 1);

    /* 第二笔超时后先进入 STOPPING：在 DMA 终止确认前，即使有空槽需求也不可复用它。 */
    assert(sparam_dma_model_timeout(&client, &second) == SPARAM_DMA_MODEL_OK);
    assert(client.timeout_count == 1);
    assert(sparam_dma_model_complete(&client, &second) == SPARAM_DMA_MODEL_STALE_CALLBACK);
    assert(sparam_dma_model_finish_stop(&client, &second) == SPARAM_DMA_MODEL_OK);

    /* 第一笔完成并被用户态读取后，它原来的槽位才允许再次用于第四笔。 */
    assert(sparam_dma_model_complete(&client, &first) == SPARAM_DMA_MODEL_OK);
    assert(client.ready_slots == 1);
    assert(sparam_dma_model_poll_readable(&client));
    assert(sparam_dma_model_begin_read(&client, &read_slot, &sequence) == SPARAM_DMA_MODEL_OK);
    assert(sequence == first.sequence);
    /* 复制尚未结束时，同一帧不能被第二个 reader 取走。 */
    assert(sparam_dma_model_begin_read(&client, &read_slot, &sequence) ==
           SPARAM_DMA_MODEL_NO_READY_RESULT);
    assert(!sparam_dma_model_poll_readable(&client));
    /* 模拟 copy_to_user() 失败：帧必须回到 READY，而不是直接丢失。 */
    assert(sparam_dma_model_finish_read(&client, read_slot, 0) == SPARAM_DMA_MODEL_OK);
    assert(client.ready_slots == 1);
    assert(client.read_fault_count == 1);
    assert(sparam_dma_model_begin_read(&client, &read_slot, &sequence) == SPARAM_DMA_MODEL_OK);
    assert(sequence == first.sequence);
    assert(sparam_dma_model_finish_read(&client, read_slot, 1) == SPARAM_DMA_MODEL_OK);

    assert(sparam_dma_model_submit(&client, 104, &reused) == SPARAM_DMA_MODEL_OK);
    assert(reused.slot_index == first.slot_index);
    assert(reused.sequence != first.sequence);

    /* 模拟已完成第一笔的重复通知：槽位已给第四笔使用，旧身份必须被拒绝。 */
    assert(sparam_dma_model_complete(&client, &first) == SPARAM_DMA_MODEL_STALE_CALLBACK);
    assert(client.stale_callback_count == 2);
    /* DMA 错误与超时一样先隔离为 STOPPING，再等同步终止后释放。 */
    assert(sparam_dma_model_dma_error(&client, &third) == SPARAM_DMA_MODEL_OK);
    assert(client.dma_error_count == 1);
    assert(sparam_dma_model_finish_stop(&client, &third) == SPARAM_DMA_MODEL_OK);
    assert(sparam_dma_model_complete(&client, &reused) == SPARAM_DMA_MODEL_OK);
    assert(client.complete_count == 2);

    puts("slot state model: PASS");
    return 0;
}
