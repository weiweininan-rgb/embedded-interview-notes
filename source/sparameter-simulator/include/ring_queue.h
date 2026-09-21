#ifndef RING_QUEUE_H
#define RING_QUEUE_H

/*
 * 有界生产者—消费者队列，用于解耦输入节奏与计算节奏。
 * 生产者快时在 not_full 等待，消费者快时在 not_empty 等待；
 * 因此不会忙等，也不会在队列满时静默丢帧。
 */
#include <pthread.h>
#include <stddef.h>
#include "sparam.h"

struct ring_queue {
    struct sparam_frame *items;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    /*
     * 关闭会唤醒两侧：已入队帧先排空，随后 pop 返回 0（EOF）。
     * 关闭后的 push 返回 -1，避免任务结束后又写入新帧。
     */
    int closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

int ring_queue_init(struct ring_queue *queue, size_t capacity);
/* 关闭不是立即丢数据：消费者仍可取完已入队帧。 */
void ring_queue_close(struct ring_queue *queue);
void ring_queue_destroy(struct ring_queue *queue);
int ring_queue_push(struct ring_queue *queue, const struct sparam_frame *frame);
int ring_queue_pop(struct ring_queue *queue, struct sparam_frame *frame);

#endif
