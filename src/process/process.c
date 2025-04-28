#include "process.h"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/shm.h>
#include "clk.h"
#include "colors.h"
#include "shared_mem.h"

pid_t process_generator_pid;
int proc_shmid = -1;

void sigIntHandler(int signum)
{
    printf(ANSI_COLOR_YELLOW "[PROCESS] Process %d received SIGINT. Terminating...\n"ANSI_COLOR_WHITE, getpid());
    destroy_clk(0);
    exit(0);
}

void sigStpHandler(int signum)
{
    // Update status in shared memory to not running
    update_process_status(proc_shmid, getpid(), 0);
    printf(ANSI_COLOR_YELLOW "[PROCESS] Process %d stopped.\n"ANSI_COLOR_WHITE, getpid());

    pause();
    signal(SIGTSTP, sigStpHandler);
}

void sigContHandler(int signum)
{
    printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d received SIGCONT. Resuming...\n"ANSI_COLOR_WHITE, getpid());

    // Update status in shared memory to running
    update_process_status(proc_shmid, getpid(), 1);

    signal(SIGCONT, sigContHandler);
}

int get_process_info(int proc_shmid, pid_t pid, process_info_t* info)
{
    if (proc_shmid == -1) return -1;
    process_info_t* shm = (process_info_t*)shmat(proc_shmid, NULL, 0);
    if ((void*)shm == (void*)-1)
    {
        perror("Error attaching shared memory in process");
        return -1;
    }

    if (shm->pid == pid)
    {
        if (info != NULL)
        {
            info->remaining_time = shm->remaining_time;
            info->status = shm->status;
            info->pid = shm->pid;
        }
        shmdt(shm);
        return 0;
    }

    shmdt(shm);
    return -1;
}

int get_time_to_run(int proc_shmid, pid_t pid)
{
    if (proc_shmid == -1) return -1;
    process_info_t* shm = (process_info_t*)shmat(proc_shmid, NULL, 0);
    if ((void*)shm == (void*)-1)
    {
        perror("Error attaching shared memory in process");
        return -1;
    }
    int remaining_time = -1;
    int status = 0;
    if (shm->pid == pid)
    {
        remaining_time = shm->remaining_time;
        status = shm->status;
    }
    shmdt(shm);
    return status ? remaining_time : -1;
}

void set_remaining_time(int proc_shmid, pid_t pid, int remaining)
{
    if (proc_shmid == -1) return;
    process_info_t* shm = (process_info_t*)shmat(proc_shmid, NULL, 0);
    if ((void*)shm == (void*)-1) return;
    if (shm->pid == pid)
    {
        shm->remaining_time = remaining;
        if (remaining == 0)
            shm->status = 0;
    }
    shmdt(shm);
}

int get_process_status(int proc_shmid, pid_t pid)
{
    if (proc_shmid == -1) return 0;
    process_info_t* shm = (process_info_t*)shmat(proc_shmid, NULL, 0);
    if ((void*)shm == (void*)-1)
    {
        perror("Error attaching shared memory in process");
        return 0;
    }

    int status = 0;
    if (shm->pid == pid)
    {
        status = shm->status;
    }

    shmdt(shm);
    return status;
}

void update_process_status(int proc_shmid, pid_t pid, int status)
{
    if (proc_shmid == -1) return;
    process_info_t* shm = (process_info_t*)shmat(proc_shmid, NULL, 0);
    if ((void*)shm == (void*)-1) return;

    if (shm->pid == pid)
    {
        shm->status = status;
    }

    shmdt(shm);
}

void run_process(int runtime)
{
    // Get shared memory ID
    proc_shmid = shmget(SHM_KEY, sizeof(process_info_t), 0666);
    if (proc_shmid == -1)
    {
        perror("[PROCESS] Error getting shared memory");
        proc_shmid = -1;
    }
    while (!get_process_status(proc_shmid, getpid()))
        usleep(1);

    printf("[PROCESS] %d Woke Up For The First Time\n", getpid());
    printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d started with runtime %d seconds.\n"ANSI_COLOR_WHITE, getpid(),
           runtime);
    sync_clk();
    int remaining = runtime;

    // Initialize status in shared memory to running
    update_process_status(proc_shmid, getpid(), 1);

    while (remaining > 0)
    {
        // Continuously check status from shared memory
        if (get_process_status(proc_shmid, getpid()))
        {
            int time_slice = 0;
            while (time_slice <= 0)
            {
                time_slice = get_time_to_run(proc_shmid, getpid());
                usleep(1000);
            }
            if (time_slice > remaining)
                time_slice = remaining;

            printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d will run for %d units (remaining: %d)\n"ANSI_COLOR_WHITE,
                   getpid(), time_slice, remaining);

            int start_time = get_clk();
            int elapsed = 0;
            while (elapsed < time_slice && get_process_status(proc_shmid, getpid()))
            {
                int now = get_clk();
                if (now != start_time)
                {
                    int diff = now - start_time;
                    for (int i = 0; i < diff && elapsed < time_slice; ++i)
                    {
                        printf(
                            ANSI_COLOR_YELLOW
                            "[PROCESS] PID %d ran for 1 second. Remaining: %d, Remaining in slice: %d\n"
                            ANSI_COLOR_WHITE,
                            getpid(), remaining - elapsed - 1, time_slice - elapsed - 1);
                        elapsed++;
                    }
                    start_time = now;
                }
                usleep(1000);
            }

            // Update remaining time
            remaining -= elapsed;
            set_remaining_time(proc_shmid, getpid(), remaining);

            printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d finished time slice, remaining: %d\n"ANSI_COLOR_WHITE,
                   getpid(), remaining);

            if (remaining > 0 && get_process_status(proc_shmid, getpid()))
            {
                // Update status in shared memory
                update_process_status(proc_shmid, getpid(), 0);
                raise(SIGTSTP); // Signal process to stop itself
            }
        }
        else
            usleep(1000); // Wait before checking status again
    }

    // Finished execution
    set_remaining_time(proc_shmid, getpid(), 0);
    update_process_status(proc_shmid, getpid(), 0);
    kill(process_generator_pid, SIGCHLD);
    printf(ANSI_COLOR_YELLOW"[PROCESS] Sending SIGCHLD to: %d\n"ANSI_COLOR_WHITE, process_generator_pid);
    printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d finished execution.\n"ANSI_COLOR_WHITE, getpid());
    destroy_clk(0);
}

int main(int argc, char* argv[])
{
    signal(SIGINT, sigIntHandler);
    signal(SIGCONT, sigContHandler);
    signal(SIGTSTP, sigStpHandler);

    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <runtime> <process_generator_pid>\n", argv[0]);
        return 1;
    }

    int runtime = atoi(argv[1]);
    process_generator_pid = atoi(argv[2]);

    if (runtime <= 0 || process_generator_pid <= 0)
    {
        if (runtime <= 0)
            fprintf(stderr, "Runtime must be a positive integer.\n");
        else
            fprintf(stderr, "process_generator_pid must be a positive integer.\n");
        return 1;
    }

    run_process(runtime);
    return 0;
}
