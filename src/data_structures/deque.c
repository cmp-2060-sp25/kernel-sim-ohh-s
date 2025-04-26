#include <stdlib.h>
#include "deque.h"

void initDeque(Deque* dq, size_t dataSize) {
    initList(&dq->list, dataSize);
}

int isDequeEmpty(Deque* dq) {
    return isListEmpty(&dq->list);
}

void pushFront(Deque* dq, void* item) {
    prepend(&dq->list, item);
}

void pushBack(Deque* dq, void* item) {
    append(&dq->list, item);
}

void* popFront(Deque* dq) {
    return removeFront(&dq->list);
}

void* popBack(Deque* dq) {
    return removeBack(&dq->list);
}

void* peekFront(Deque* dq) {
    return getFront(&dq->list);
}

void* peekBack(Deque* dq) {
    return getBack(&dq->list);
}

void clearDeque(Deque* dq) {
    clearList(&dq->list);
}
