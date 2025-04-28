#include "shared_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <string.h>
#include "colors.h"

int create_shared_memory(key_t key)
{
    int shmid = shmget(key, sizeof(process_info_t), IPC_CREAT | 0666);
    if (shmid == -1)
    {
        perror("Error creating shared memory");
        return -1;
    }

    // Initialize the shared memory
    process_info_t* shm = (process_info_t*)shmat(shmid, NULL, 0);
    if ((void*)shm == (void*)-1)
    {
        perror("Error attaching shared memory");
        return -1;
    }

    shm->status = -1;
    shm->pid = -1;
    shm->remaining_time = -1;

    shmdt(shm);
    printf(ANSI_COLOR_BLUE"[SHARED_MEM] Shared memory created with ID: %d\n"ANSI_COLOR_RESET, shmid);
    return shmid;
}

void write_process_info(int shm_id, int pid, int remaining_time, int status)
{
    process_info_t* shm = (process_info_t*)shmat(shm_id, NULL, 0);
    if ((void*)shm == (void*)-1) return;
    shm->pid = pid;
    shm->remaining_time = remaining_time;
    shm->status = status;
    shmdt(shm);
}

process_info_t read_process_info(int shm_id, int pid)
{
    process_info_t process_info = {.status = -1, .pid = -1, .remaining_time = -1};

    process_info_t* shm = (process_info_t*)shmat(shm_id, NULL, 0);
    if ((void*)shm == (void*)-1 || shm->pid != pid) return process_info;

    process_info.remaining_time = (shm->pid == pid) ? shm->remaining_time : -1;
    process_info.pid = (shm->pid == pid) ? shm->pid : -1;
    process_info.status = (shm->pid == pid) ? shm->status : -1;
    shmdt(shm);
    return process_info;
}

int get_shared_memory(key_t key)
{
    int shmid = shmget(key, sizeof(process_info_t) * MAX_PROCESSES, 0666);
    if (shmid == -1)
    {
        perror("Error getting shared memory");
        return -1;
    }
    return shmid;
}

void cleanup_shared_memory(int shmid)
{
    if (shmid != -1)
    {
        shmctl(shmid, IPC_RMID, NULL);
        printf(ANSI_COLOR_BLUE"[SHARED_MEM] Shared memory with ID %d removed\n"ANSI_COLOR_RESET, shmid);
    }
}
