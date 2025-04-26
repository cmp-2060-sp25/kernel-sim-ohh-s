#pragma once

#include <stddef.h>
#include "linked_list.h"

typedef struct {
    linked_list list;
} Queue;

void initQueue(Queue* q, size_t dataSize);
int isQueueEmpty(Queue* q);
void enqueue(Queue* q, void* item);
void* dequeue(Queue* q);
void* peekQueue(Queue* q);
void clearQueue(Queue* q);