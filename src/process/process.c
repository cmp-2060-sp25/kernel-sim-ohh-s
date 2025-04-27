#include "process.h"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "clk.h"
#include "colors.h"

#define LOCK_FILE "/tmp/process.lock"
pid_t process_generator_pid;

volatile sig_atomic_t is_running = 0;

void sigIntHandler(int signum)
{
    printf(ANSI_COLOR_YELLOW "[PROCESS] Process %d received SIGINT. Terminating...\n"ANSI_COLOR_WHITE, getpid());
    // Check if the lock file contains the current process's PID
    int lock_fd = open(LOCK_FILE, O_RDONLY);
    if (lock_fd != -1)
    {
        char pid_buffer[16];
        ssize_t bytes_read = read(lock_fd, pid_buffer, sizeof(pid_buffer) - 1);
        if (bytes_read > 0)
        {
            pid_buffer[bytes_read] = '\0'; // Null-terminate the string
            pid_t lock_pid = (pid_t)atoi(pid_buffer);
            if (lock_pid == getpid())
            {
                unlink(LOCK_FILE); // Only unlink if the PID matches
            }
        }
        close(lock_fd);
    }
    destroy_clk(0);
    exit(0);
}

void sigStpHandler(int signum)
{
    if (access("/tmp/process.lock", F_OK) == 0)
    {
        // Check if the lock file contains the current process's PID
        int lock_fd = open(LOCK_FILE, O_RDONLY);
        // if (lock_fd != -1)
        // {
        //     char pid_buffer[16];
        //     ssize_t bytes_read = read(lock_fd, pid_buffer, sizeof(pid_buffer) - 1);
        //     if (bytes_read > 0)
        //     {
        //         pid_buffer[bytes_read] = '\0'; // Null-terminate the string
        //         pid_t lock_pid = (pid_t)atoi(pid_buffer);
        //         if (lock_pid == getpid())
        //         {
        //             unlink(LOCK_FILE); // Only unlink if the PID matches
        //             printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d received SIGTSTP. Pausing...\n"ANSI_COLOR_WHITE,
        //                    getpid());
        //             is_running = 0; // Set flag to not running
        //         }
        //         fprintf(stderr, "[PROCESS] Trying to remove a lock file that isnt mine\n");
        //     }
        //     close(lock_fd);
        // }
        unlink(LOCK_FILE); // Only unlink if the PID matches
        close(lock_fd);

    }
    is_running = 0; // Set flag to not running

    pause();
    signal(SIGTSTP, sigStpHandler);
}

void sigContHandler(int signum)
{
    // Robustly acquire the lock file
    while (1)
    {
        int lock_fd = open(LOCK_FILE, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (lock_fd != -1)
        {
            // Success: write PID and break
            char pid_buffer[16];
            snprintf(pid_buffer, sizeof(pid_buffer), "%d\n", getpid());
            write(lock_fd, pid_buffer, strlen(pid_buffer));
            close(lock_fd);
            break;
        }
        else if (errno == EEXIST)
        {
            // Lock file exists, check if it's ours
            lock_fd = open(LOCK_FILE, O_RDONLY);
            if (lock_fd != -1)
            {
                char pid_buffer[16];
                ssize_t bytes_read = read(lock_fd, pid_buffer, sizeof(pid_buffer) - 1);
                if (bytes_read > 0)
                {
                    pid_buffer[bytes_read] = '\0';
                    pid_t lock_pid = (pid_t)atoi(pid_buffer);
                    if (lock_pid == getpid())
                    {
                        // Already ours, just break
                        close(lock_fd);
                        break;
                    }
                }
                close(lock_fd);
            }
            // Not ours, wait and retry
            usleep(1000);
        }
        else
        {
            perror("[PROCESS] Error creating lock file");
            usleep(1000);
        }
    }
    printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d received SIGCONT. Resuming...\n"ANSI_COLOR_WHITE, getpid());
    is_running = 1;
    signal(SIGCONT, sigContHandler);
}

void run_process(int runtime)
{
    printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d started with runtime %d seconds.\n"ANSI_COLOR_WHITE, getpid(),
           runtime);

    sync_clk();
    int current_time = get_clk();
    int start_time = 0;

    is_running = 1; // Initial state is running

    while (runtime > 0)
    {
        if (is_running)
        {
            start_time = get_clk();
            int chunk_start_time = start_time;
            int chunk_elapsed = 0;

            while (chunk_elapsed < 1 && is_running)
            {
                current_time = get_clk();
                if (current_time != chunk_start_time)
                {
                    int time_diff = current_time - chunk_start_time;
                    chunk_elapsed += time_diff;
                    chunk_start_time = current_time;
                    //printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d running. Chunk progress: %d/%d seconds.\n"ANSI_COLOR_WHITE, getpid(), chunk_elapsed, 1);
                }
                usleep(1000); // 1ms, check clock frequently but not too often
            }

            // If we completed the chunk (wasn't interrupted by signal)
            if (chunk_elapsed == 1)
            {
                runtime -= 1;
                printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d Remaining time: %d seconds.\n"ANSI_COLOR_WHITE, getpid(),
                       runtime);
            }
        }
        else
        {
            // Not running, just wait until a signal changes our state
            usleep(1000); // 1ms pause to avoid busy waiting
        }
    }

    if (runtime <= 0)
    {
        // Only send SIGCHLD if we actually completed the runtime
        kill(process_generator_pid, SIGCHLD);
        printf(ANSI_COLOR_YELLOW"[PROCESS] Sending SIGCHILD to: %d\n"ANSI_COLOR_WHITE, process_generator_pid);
        printf(ANSI_COLOR_YELLOW"[PROCESS] Process %d finished execution.\n"ANSI_COLOR_WHITE, getpid());
    }

    destroy_clk(0);
}

int main(int argc, char* argv[])
{
    signal(SIGINT, sigIntHandler);
    signal(SIGCONT, sigContHandler);
    signal(SIGTSTP, sigStpHandler);
    // raise(SIGTSTP);
    printf("[PROCESS] Woke Up For The First Time\n");
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

    // is_running = 0; // Do not run until SIGCONT is received
    // while (!is_running)
    // {
    //     usleep(1); // Wait for SIGCONT to set is_running = 1
    // }
    //

    run_process(runtime);
    unlink(LOCK_FILE);
    return 0;
}
