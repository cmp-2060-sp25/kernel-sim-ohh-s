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

// Use pointers for both possible queue types
min_heap_t* min_heap_queue = NULL;
Queue* rr_queue = NULL;

extern int msgid;
extern int scheduler_type;
extern int quantum;

void child_cleanup()
{
    signal(SIGCHLD, child_cleanup);
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
        while ((crt_clk = get_clk()) == old_clk) usleep(10000);
        old_clk = crt_clk;
        receive_processes(msgid);

        // --------- Scheduling Algorithm Dispatch ---------
        if (scheduler_type == 1) // HPF
            running_process = hpf(min_heap_queue, running_process, crt_clk);
        else if (scheduler_type == 2) // SRTN
            running_process = srtn(min_heap_queue, crt_clk);
        else if (scheduler_type == 0) // RR
            running_process = rr(rr_queue, running_process, crt_clk, quantum);
        else
        {
            fprintf(stderr, "Unknown scheduler_type: %d\n", scheduler_type);
            exit(EXIT_FAILURE);
        }
    }
}

void receive_processes(const int msgid)
{
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
        *new_pcb = received_pcb;

        if (scheduler_type == 1 || scheduler_type == 2)
            min_heap_insert(min_heap_queue, new_pcb);
        else if (scheduler_type == 0)
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
    completed_process_count = 0;
    running_process = NULL;

    if (scheduler_type == 1 || scheduler_type == 2)
    {
        min_heap_queue = create_min_heap(100, compare_processes);
        if (min_heap_queue == NULL)
        {
            perror("Failed to create min_heap_queue");
            return -1;
        }
    }
    else if (scheduler_type == 0)
    {
        rr_queue = (Queue*)malloc(sizeof(Queue));
        if (rr_queue == NULL)
        {
            perror("Failed to allocate memory for rr_queue");
            return -1;
        }
        initQueue(rr_queue, sizeof(PCB*));
        // }

        // Initialize process_count if not done elsewhere
        process_count = 0;

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
        finished_processes = malloc(100 * sizeof(PCB*));
        if (finished_processes == NULL)
        {
            perror("Failed to allocate memory for finished processes");
            fclose(log_file);
            return -1;
        }

        printf("Scheduler initialized successfully at time %d\n", current_time);
        return 0;
    }
}
