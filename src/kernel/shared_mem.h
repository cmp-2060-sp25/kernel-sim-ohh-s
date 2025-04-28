#pragma once

#include <sys/types.h>

typedef struct
{
    pid_t pid; // Process ID
    int time_to_run; // Time to run this process for
    int status; // 1 for running, 0 for stopped/paused
    int current_clk; // Handshake: scheduler writes current clock here
} process_info_t;

int create_shared_memory(key_t key);
int get_shared_memory(key_t key);
void cleanup_shared_memory(int shmid);
void write_process_info(int shm_id, int pid, int remaining_time, int status, int current_clk);
process_info_t read_process_info(int shm_id, int pid);

#define SHM_KEY 400
#define MAX_PROCESSES 100
