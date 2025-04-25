#include "scheduler.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <unistd.h>
#include "clk.h"
#include "scheduler_utils.h"

PCB** pcbs;
extern int msgid;


void run_scheduler()
{
    signal(SIGINT, scheduler_cleanup);
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
        // printf("old_clk: %d, crt_clk: %d\n", old_clk, crt_clk);
        receive_processes(msgid);
    }
}


void receive_processes(const int msgid)
{
    PCB received_pcb;

    size_t recv_val = msgrcv(msgid, &received_pcb, sizeof(PCB), 1, IPC_NOWAIT);

    if (recv_val == -1)
    {
        if (errno != ENOMSG)
        {
            perror("Error receiving message");
        }
        return; // Exit if no messages or error
    }

    // Process messages
    while (recv_val != -1)
    {
        printf("Received process ID: %d, arrival time: %d, remaining_time: %d at %d\n",
               received_pcb.id, received_pcb.arrival_time, received_pcb.remaining_time, get_clk());

        // Allocate memory for new PCB
        pcbs[process_count] = (PCB*)malloc(sizeof(PCB));
        if (!pcbs[process_count])
        {
            perror("Failed to allocate memory for PCB");
            break;
        }

        // Copy received PCB data
        *pcbs[process_count] = received_pcb;

        // Increment process count
        process_count++;

        // min_heap_insert(ready_queue, pcbs[process_count-1]);

        recv_val = msgrcv(msgid, &received_pcb, sizeof(PCB) - sizeof(long), 1, IPC_NOWAIT);
    }
}


void scheduler_cleanup(int signum)
{
    // Free allocated memory for pcbs before exiting
    for (int i = 0; i < process_count; i++)
    {
        if (pcbs[i] != NULL)
            free(pcbs[i]);
    }
    free(pcbs);

    generate_statistics();
    if (log_file)
        fclose(log_file);

    // Remove message queue
    if (msgid != -1)
    {
        msgctl(msgid, IPC_RMID, NULL);
    }

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
    ready_queue = create_min_heap(100, compare_processes);
    if (ready_queue == NULL)
    {
        perror("Failed to create ready queue");
        return -1;
    }

    // Allocate memory for pcbs array
    pcbs = (PCB**)malloc(MAX_INPUT_PROCESSES * sizeof(PCB*)); // Adjust size as needed

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
