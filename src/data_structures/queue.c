#include "queue.h"

void initQueue(Queue* q, size_t dataSize) {
    initList(&q->list, dataSize);
}

int isQueueEmpty(Queue* q) {
    return isListEmpty(&q->list);
}

void enqueue(Queue* q, void* item) {
    append(&q->list, item);
}

void* dequeue(Queue* q) {
    return removeFront(&q->list);
}

void* peekQueue(Queue* q) {
    return getFront(&q->list);
}

void clearQueue(Queue* q) {
    clearList(&q->list);
}
