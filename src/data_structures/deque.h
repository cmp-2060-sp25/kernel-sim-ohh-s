#pragma once
#include <stddef.h>
#include "linked_list.h"

typedef struct {
    linked_list list;
} Deque;

// Deque API
void initDeque(Deque* dq, size_t dataSize);
int isDequeEmpty(Deque* dq);
void pushFront(Deque* dq, void* item);
void pushBack(Deque* dq, void* item);
void* popFront(Deque* dq);
void* popBack(Deque* dq);
void* peekFront(Deque* dq);
void* peekBack(Deque* dq);
void clearDeque(Deque* dq);