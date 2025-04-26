#include <stdio.h>

#include "headers.h"
#include "min_heap.h"
#include "pcb.h"

// Global variables
int process_count = 0;
PCB* running_process = NULL;
min_heap_t* ready_queue = NULL;
FILE* log_file = NULL;
