#ifndef SCHEDULER_UTILITIES_H
#define SCHEDULER_UTILITIES_H

#include "headers.h"
#include "pcb.h"
#include "min_heap.h"

// Function prototypes
int compare_processes(const void* p1, const void* p2);
PCB* hpf(min_heap_t* ready_queue, PCB* running_process, int current_time);
void log_process_state(PCB* process, char* state, int time);
void generate_statistics();


#endif
