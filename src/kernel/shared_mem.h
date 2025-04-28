#pragma once

#include <sys/types.h>

// Structure for process information in shared memory
typedef struct
{
    pid_t pid;
    int remaining_time;
    int status; // 1 for running, 0 for paused
} process_info_t;

// Function prototypes
int create_shared_memory(key_t key);
int get_shared_memory(key_t key);
void cleanup_shared_memory(int shmid);
void write_process_info(int shm_id, int pid, int remaining_time, int status);
process_info_t read_process_info(int shm_id, int pid);


#define SHM_KEY 400
#define MAX_PROCESSES 100
