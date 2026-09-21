/* 用刻意很小的队列验证 FIFO 顺序和关闭/EOF 语义。 */
#include "ring_queue.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>

struct context { struct ring_queue *queue; int count; };

static void *producer(void *argument)
{
    /* 容量仅为 2，此循环也会迫使生产者等待消费者。 */
    struct context *context = argument;
    struct sparam_frame frame = { .sample_count = 2 };
    int i;
    for (i = 0; i < context->count; ++i) {
        frame.frequency_hz = i;
        assert(ring_queue_push(context->queue, &frame) == 0);
    }
    /* 最后一帧入队后关闭，表示输入正常结束。 */
    ring_queue_close(context->queue);
    return NULL;
}

int main(void)
{
    struct ring_queue queue;
    struct context context = { .queue = &queue, .count = 100 };
    struct sparam_frame frame;
    pthread_t thread;
    int received = 0;
    assert(ring_queue_init(&queue, 2) == 0);
    assert(pthread_create(&thread, NULL, producer, &context) == 0);
    while (ring_queue_pop(&queue, &frame) > 0) {
        assert((int)frame.frequency_hz == received);
        received++;
    }
    assert(received == context.count);
    pthread_join(thread, NULL);
    ring_queue_destroy(&queue);
    puts("queue tests: PASS");
    return 0;
}
