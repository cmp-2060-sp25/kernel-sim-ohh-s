#include "min_heap.h"
#include <stdlib.h>
#include <string.h>

static void swap(void** a, void** b) {
    void* temp = *a;
    *a = *b;
    *b = temp;
}

static void heapify_up(min_heap_t* heap, int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap->compare(heap->data[index], heap->data[parent]) < 0) {
            swap(&heap->data[index], &heap->data[parent]);
            index = parent;
        } else {
            break;
        }
    }
}

static void heapify_down(min_heap_t* heap, int index) {
    int left, right, smallest;
    while (1) {
        left = 2 * index + 1;
        right = 2 * index + 2;
        smallest = index;

        if (left < heap->size && heap->compare(heap->data[left], heap->data[smallest]) < 0)
            smallest = left;
        if (right < heap->size && heap->compare(heap->data[right], heap->data[smallest]) < 0)
            smallest = right;

        if (smallest != index) {
            swap(&heap->data[index], &heap->data[smallest]);
            index = smallest;
        } else {
            break;
        }
    }
}

min_heap_t* create_min_heap(int capacity, int (*compare)(const void*, const void*)) {
    min_heap_t* heap = malloc(sizeof(min_heap_t));
    heap->data = malloc(sizeof(void*) * capacity);
    heap->size = 0;
    heap->capacity = capacity;
    heap->compare = compare;
    return heap;
}

void min_heap_insert(min_heap_t* heap, void* item) {
    if (heap->size == heap->capacity) {
        heap->capacity *= 2;
        heap->data = realloc(heap->data, sizeof(void*) * heap->capacity);
    }
    heap->data[heap->size++] = item;
    heapify_up(heap, heap->size - 1);
}

void* min_heap_get_min(min_heap_t* heap) {
    return heap->size > 0 ? heap->data[0] : NULL;
}

void* min_heap_extract_min(min_heap_t* heap) {
    if (heap->size == 0) return NULL;

    void* min = heap->data[0];
    heap->data[0] = heap->data[--heap->size];
    heapify_down(heap, 0);

    return min;
}

int min_heap_is_empty(min_heap_t* heap) {
    return heap->size == 0;
}

void destroy_min_heap(min_heap_t* heap) {
    free(heap->data);
    free(heap);
}
