#include "scheduler.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <unistd.h>
#include "clk.h"
#include "scheduler_utils.h"
#include "min_heap.h"
#include "queue.h"
#include <sys/types.h>
#include <sys/wait.h>

#include "headers.h"
// Use pointers for both possible queue types
min_heap_t* min_heap_queue = NULL;
Queue* rr_queue = NULL;

extern int msgid;
extern int scheduler_type;
extern int quantum;

void child_cleanup()
{
    printf("[SCHEDULER] CHILD_CLEANUP CALLED\n");
    signal(SIGCHLD, child_cleanup);
    if (running_process)
        free(running_process);
    else
        printf("Requested to cleanup none????");
    running_process = NULL;
}

void run_scheduler()
{
    signal(SIGINT, scheduler_cleanup);
    signal(SIGCHLD, child_cleanup);
    sync_clk();

    if (init_scheduler() == -1)
    {
        fprintf(stderr, "Failed to initialize scheduler\n");
        return;
    }

    int crt_clk = get_clk();
    int old_clk = -1;

    while (1)
    {
        while ((crt_clk = get_clk()) == old_clk) usleep(1000);
        old_clk = crt_clk;

        receive_processes();

        // --------- Scheduling Algorithm Dispatch ---------
        if (scheduler_type == HPF) // HPF
        {
            running_process = hpf(min_heap_queue, crt_clk);
            if (running_process == NULL) continue; // there is no process to run

            // Resume the process
            kill(running_process->pid, SIGCONT);
            printf("RUNNING PID %d\n", running_process->pid);

            // Polling loop to check if process still exists
            while (1)
            {
                // Try sending signal 0 to process - this doesn't actually send a signal
                // but checks if the process exists and we have permission to send signals
                if (kill(running_process->pid, 0) < 0)
                {
                    if (errno == ESRCH)
                    {
                        // Process no longer exists
                        printf("Process %d has terminated\n", running_process->pid);
                        break;
                    }
                }

                usleep(1000); // 1ms

                // Allow scheduler to receive messages during polling
                receive_processes();
            }

            // Process is done
            printf("child exited\n");
        }
        else if (scheduler_type == SRTN)
        {
            running_process = srtn(min_heap_queue, crt_clk);
            if (running_process == NULL) continue; // there is no process to run

            kill(running_process->pid,SIGCONT);
            int crt_time = get_clk();
            int old_time = crt_time;
            while ((crt_time = get_clk()) == old_time); // busy wait the scheduler for a clk
            kill(running_process->pid,SIGSTOP); // Pause the child for context switching
            running_process->status = READY;
            running_process->remaining_time -= crt_time - old_time;
            running_process->last_run_time = crt_time;
            if (running_process->remaining_time)
                min_heap_insert(min_heap_queue, running_process);
        }
        else if (scheduler_type == RR)
        {
            running_process = rr(rr_queue, crt_clk);
            if (running_process == NULL) continue; // there is no process to run
            kill(running_process->pid,SIGCONT);
            int crt_time = get_clk();
            int time_to_run = (running_process->remaining_time < quantum) 
                ? running_process->remaining_time 
                : quantum;
            int end_time = crt_time + time_to_run;
            while ((crt_time = get_clk()) < end_time); // busy wait the scheduler for a quantum
            kill(running_process->pid,SIGSTOP); // Pause the child for context switching
            running_process->status = READY;
            running_process->remaining_time -= time_to_run;
            running_process->last_run_time = crt_time;
            if (running_process->remaining_time)
                enqueue(rr_queue, running_process);
            else {
                running_process->status = TERMINATED;
                running_process->finish_time = crt_time;
                running_process->waiting_time = (running_process->finish_time - running_process->arrival_time) - running_process->runtime;
                running_process->turnaround_time = running_process->finish_time - running_process->arrival_time;
                running_process->weighted_turnaround = (float)running_process->turnaround_time / running_process->runtime;
                log_process_state(running_process, "finished", current_time);
            }
        }

        else
        {
            fprintf(stderr, "Unknown scheduler_type: %d\n", scheduler_type);
            exit(EXIT_FAILURE);
        }
    }
}

void receive_processes(void)
{
    if (msgid == -1)
        return;

    PCB received_pcb;
    size_t recv_val = msgrcv(msgid, &received_pcb, sizeof(PCB), 1, IPC_NOWAIT);

    if (recv_val == -1)
    {
        if (errno != ENOMSG)
            perror("Error receiving message");
        return;
    }

    while (recv_val != -1)
    {
        printf("Received process ID: %d, arrival time: %d, remaining_time: %d at %d\n",
               received_pcb.id, received_pcb.arrival_time, received_pcb.remaining_time, get_clk());

        PCB* new_pcb = (PCB*)malloc(sizeof(PCB));
        if (!new_pcb)
        {
            perror("Failed to allocate memory for PCB");
            break;
        }
        *new_pcb = received_pcb; // shallow copy, doesnt matter

        if (scheduler_type == HPF || scheduler_type == SRTN)
            min_heap_insert(min_heap_queue, new_pcb);
        else if (scheduler_type == RR)
            enqueue(rr_queue, new_pcb);

        process_count++;
        recv_val = msgrcv(msgid, &received_pcb, sizeof(PCB) - sizeof(long), 1, IPC_NOWAIT);
    }
}

void scheduler_cleanup(int signum)
{
    signal(SIGINT, scheduler_cleanup);

    generate_statistics();
    if (log_file)
        fclose(log_file);

    // Remove message queue
    if (msgid != -1)
    {
        msgctl(msgid, IPC_RMID, NULL);
    }

    destroy_clk(0);
    printf("[SCHEDULER] Resources cleaned up \n");

    if (signum != 0)
    {
        exit(signum);
    }
}

int init_scheduler()
{
    int current_time = get_clk();
    process_count = 0;
    running_process = NULL;

    if (scheduler_type == HPF || scheduler_type == SRTN)
    {
        min_heap_queue = create_min_heap(MAX_INPUT_PROCESSES, compare_processes);
        if (min_heap_queue == NULL)
        {
            perror("Failed to create min_heap_queue");
            return -1;
        }
    }
    else if (scheduler_type == RR)
    {
        rr_queue = (Queue*)malloc(sizeof(Queue));
        if (rr_queue == NULL)
        {
            perror("Failed to allocate memory for rr_queue");
            return -1;
        }
        initQueue(rr_queue, sizeof(PCB*));
    }

    // Init IPC
    key_t key = ftok("process_generator", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1)
    {
        perror("Error getting message queue");
        return -1;
    }

    log_file = fopen("scheduler.log", "w");
    if (log_file == NULL)
    {
        perror("Failed to open log file");
        return -1;
    }
    fprintf(log_file, "#At\ttime\tx\tprocess\ty\tstate\tarr\tw\ttotal\tz\tremain\ty\twait\tk\n");

    printf("Scheduler initialized successfully at time %d\n", current_time);
    return 0;
}
