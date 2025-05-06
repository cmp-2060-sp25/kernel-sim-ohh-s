#pragma once

#include <stdio.h>
#include "pcb.h"
#include "min_heap.h"

void scheduler_cleanup(int signum);
void run_scheduler();
int init_scheduler();
void generate_statistics();
int compare_processes(const void* a, const void* b);
void log_process_state(PCB* process, char* state, int time);
int receive_processes(void);
void child_cleanup();

// Global variables declarations (extern)
extern int current_time;
extern int process_count;  
extern int completed_process_count;
extern PCB* running_process;
extern min_heap_t* ready_queue;
extern int msg_queue_id;
extern FILE* log_file;
