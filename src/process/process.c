#include "process.h"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "clk.h"

#define LOCK_FILE "/tmp/process.lock"
pid_t process_generator_pid;

volatile sig_atomic_t is_running = 0;

void sigIntHandler(int signum)
{
    printf("Process %d received SIGINT. Terminating...\n", getpid());
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
                printf("Process %d received SIGTSTP. Pausing...\n", getpid());
                is_running = 0; // Set flag to not running
            }
        }
        close(lock_fd);
    }
    pause();
    signal(SIGTSTP, sigStpHandler);
}

void sigContHandler(int signum)
{
    int lock_fd = open(LOCK_FILE, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (lock_fd == -1)
    {
        if (errno == EEXIST)
        {
            fprintf(stderr, "Error: Another instance of the process is already running.\n");
            kill(getpid(), SIGTSTP);
            return;
        }
        else
        {
            perror("Error creating lock file");
            kill(getpid(), SIGTSTP);
            return;
        }
    }

    // Write the current process's PID to the lock file
    char pid_buffer[16];
    snprintf(pid_buffer, sizeof(pid_buffer), "%d\n", getpid());
    write(lock_fd, pid_buffer, strlen(pid_buffer));
    close(lock_fd);

    printf("Process %d received SIGCONT. Resuming...\n", getpid());
    is_running = 1; // Set flag to running
    signal(SIGCONT, sigContHandler);
}

void run_process(int runtime)
{
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
                    printf("Process %d running. Chunk progress: %d/%d seconds.\n",
                           getpid(), chunk_elapsed, 1);
                }
                usleep(1000); // 1ms, check clock frequently but not too often
            }

            // If we completed the chunk (wasn't interrupted by signal)
            if (chunk_elapsed == 1)
            {
                runtime -= 1;
                printf("Process %d completed execution chunk. Remaining time: %d seconds.\n",
                       getpid(), runtime);
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
        printf("Sending SIGCHILD to: %d\n", process_generator_pid);
        printf("Process %d finished execution.\n", getpid());
    }

    destroy_clk(0);
}

int main(int argc, char* argv[])
{
    signal(SIGTSTP, sigStpHandler);
    signal(SIGINT, sigIntHandler);
    signal(SIGCONT, sigContHandler);

    // Create a lock file to ensure only one instance is running
    int lock_fd = open(LOCK_FILE, O_CREAT | O_EXCL | O_RDWR, 0644);
    if (lock_fd == -1)
    {
        if (errno == EEXIST)
        {
            fprintf(stderr, "Error: Another instance of the process is already running.\n");
            return 1;
        }
        else
        {
            perror("Error creating lock file");
            return 1;
        }
    }

    // Write the current PID to the lock file
    ftruncate(lock_fd, 0);
    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%d\n", getpid());
    write(lock_fd, pid_str, strlen(pid_str));

    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <runtime> <process_generator_pid>\n", argv[0]);
        unlink(LOCK_FILE); // Remove the lock file
        close(lock_fd);
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
        unlink(LOCK_FILE); // Remove the lock file
        close(lock_fd);
        return 1;
    }

    printf("Process %d started with runtime %d seconds.\n", getpid(), runtime);
    run_process(runtime);
    unlink(LOCK_FILE); // Remove the lock file when the process finishes
    close(lock_fd);
    return 0;
}
