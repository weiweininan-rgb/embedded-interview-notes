/*
 * 线程安全的有界 FIFO，在采集与处理间提供背压。
 * 所有 head/tail/count/closed 的读写都必须持有 mutex；条件变量只负责等待，
 * 被唤醒后仍要在 while 中重新检查条件，防止伪唤醒或其他线程先一步改变状态。
 */
#include "ring_queue.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int ring_queue_init(struct ring_queue *queue, size_t capacity)
{
    /* items 按帧复制，而不是保存调用者栈上 frame 的指针。 */
    if (!queue || !capacity) return -1;
    memset(queue, 0, sizeof(*queue));
    queue->items = calloc(capacity, sizeof(*queue->items));
    if (!queue->items) return -1;
    queue->capacity = capacity;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
    return 0;
}

void ring_queue_close(struct ring_queue *queue)
{
    /*
     * 唤醒两侧等待者，确保关闭时生产者和消费者都不会卡住。
     * 消费者被唤醒后若仍有 count，会继续取帧；只有 count=0 才得到 EOF。
     */
    pthread_mutex_lock(&queue->mutex);
    queue->closed = 1;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

void ring_queue_destroy(struct ring_queue *queue)
{
    if (!queue) return;
    free(queue->items);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->mutex);
}

int ring_queue_push(struct ring_queue *queue, const struct sparam_frame *frame)
{
    /*
     * 队列满时阻塞生产者，不悄悄丢弃测量帧。
     * tail 指向下一次写入位置；写入后循环前移，并通知可能正在等数据的消费者。
     */
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == queue->capacity && !queue->closed)
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    if (queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        errno = ECANCELED;
        return -1;
    }
    queue->items[queue->tail] = *frame;
    queue->tail = (queue->tail + 1U) % queue->capacity;
    queue->count++;
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

int ring_queue_pop(struct ring_queue *queue, struct sparam_frame *frame)
{
    /*
     * 队列为空时等待生产者；关闭后先排空已有帧，只在无帧可取时返回 0。
     * head 指向下一次取出位置；取出后通知可能因队列满而等待的生产者。
     */
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->closed)
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    if (queue->count == 0 && queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }
    *frame = queue->items[queue->head];
    queue->head = (queue->head + 1U) % queue->capacity;
    queue->count--;
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return 1;
}
