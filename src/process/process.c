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
            kill(getpid(),SIGTSTP);
            return;
        }
        else
        {
            perror("Error creating lock file");
            kill(getpid(),SIGTSTP);
            return;
        }
    }

    // Write the current process's PID to the lock file
    char pid_buffer[16];
    snprintf(pid_buffer, sizeof(pid_buffer), "%d\n", getpid());
    write(lock_fd, pid_buffer, strlen(pid_buffer));
    close(lock_fd);

    printf("Process %d received SIGCONT. Resuming...\n", getpid());
    signal(SIGCONT, sigContHandler);
}

void run_process(int runtime)
{
    sync_clk();
    int start_time = get_clk(); // Get the current clock time
    int current_time = start_time; // Get the updated clock time

    while (runtime > 0)
    {
        while ((start_time = get_clk()) == current_time)
            usleep(1000); // 10us
        runtime -= (start_time - current_time); // Decrement runtime
        current_time = start_time;
        printf("Process %d running. Remaining time: %d seconds.\n", getpid(), runtime);
    }

    kill(process_generator_pid,SIGCHLD);
    printf("Sending SIGCHILD to: %d\n", process_generator_pid);
    printf("Process %d finished execution.\n", getpid());
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
