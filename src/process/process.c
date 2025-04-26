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
short hasReceivedSIGSTOP = 0;

void sigIntHandler(int signum)
{
    printf("Process %d received SIGINT. Terminating...\n", getpid());
    unlink(LOCK_FILE);
    destroy_clk(0);
    exit(0);
}

void sigStpHandler(int signum)
{
    hasReceivedSIGSTOP = 1;
    printf("Process %d received SIGTSTP. Pausing...\n", getpid());
    pause();
    signal(SIGTSTP, sigStpHandler);
}

void sigContHandler(int signum)
{
    printf("Process %d received SIGCONT. Resuming...\n", getpid());
    signal(SIGCONT, sigContHandler);
}

void run_process(int runtime)
{
    sync_clk();
    int start_time = get_clk(); // Get the current clock time

    while (runtime > 0)
    {
        int current_time = get_clk(); // Get the updated clock time

        if (current_time > start_time)
        {
            runtime -= (current_time - start_time); // Decrement runtime
            start_time = current_time; // Update start_time to current_time
        }

        printf("Process %d running. Remaining time: %d seconds.\n", getpid(), runtime);
        sleep(1);
    }
    // Extra
    kill(getpgrp(), SIGCHLD);
    printf("Process %d finished execution.\n", getpid());
    destroy_clk(0);
}


int main(int argc, char* argv[])
{
    signal(SIGINT, sigIntHandler);
    signal(SIGTSTP, sigStpHandler);
    signal(SIGCONT, sigContHandler);

    // Create a lock file to ensure only one instance is running
    int lock_fd = open(LOCK_FILE, O_CREAT | O_EXCL, 0644);
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

    signal(SIGINT, sigIntHandler);
    signal(SIGTSTP, sigStpHandler);
    signal(SIGCONT, sigContHandler);

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <runtime>\n", argv[0]);
        unlink(LOCK_FILE); // Remove the lock file
        close(lock_fd);
        return 1;
    }

    int runtime = atoi(argv[1]);
    if (runtime <= 0)
    {
        fprintf(stderr, "Runtime must be a positive integer.\n");
        unlink(LOCK_FILE); // Remove the lock file
        close(lock_fd);
        return 1;
    }

    printf("Process %d started with runtime %d seconds.\n", getpid(), runtime);
    while (!hasReceivedSIGSTOP);
    run_process(runtime);

    unlink(LOCK_FILE); // Remove the lock file when the process finishes
    close(lock_fd);
    return 0;
}
