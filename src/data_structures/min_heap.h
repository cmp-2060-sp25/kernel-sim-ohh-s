#pragma once

#include <stddef.h>

typedef struct min_heap {
    void** data;
    int size;
    int capacity;
    int (*compare)(const void*, const void*);
} min_heap_t;

min_heap_t* create_min_heap(int capacity, int (*compare)(const void*, const void*));
void min_heap_insert(min_heap_t* heap, void* item);
void* min_heap_get_min(min_heap_t* heap);
void* min_heap_extract_min(min_heap_t* heap);
int min_heap_is_empty(min_heap_t* heap);
void destroy_min_heap(min_heap_t* heap);