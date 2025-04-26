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
#include "headers.h"

extern finishedProcessInfo** finished_process_info;
// Use pointers for both possible queue types
min_heap_t* min_heap_queue = NULL;
Queue* rr_queue = NULL;

extern int msgid;
extern int scheduler_type;
extern int quantum;
extern finishedProcessInfo** finished_process_info;
extern int finished_processes_count;

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

        int receive_status = receive_processes();
        if (receive_status == -2 && !process_count)
        {
            printf("Message queue has been closed. Terminating scheduler.\n");
            break; // Exit the scheduling loop
        }

        // --------- Scheduling Algorithm Dispatch ---------
        if (scheduler_type == HPF) // HPF
        {
            running_process = hpf(min_heap_queue, crt_clk);
            if (running_process == NULL) continue; // there is no process to run

            // Resume the process
            kill(running_process->pid, SIGCONT);
            printf("RUNNING PID %d\n", running_process->pid);

            // Polling loop to check if process still exists
            pid_t rpid = running_process != NULL ? running_process->pid : -1;
            do
            {
                // Try sending signal 0 to process - this doesn't actually send a signal
                // but checks if the process exists, and we have permission to send signals
                if (kill(rpid, 0) < 0)
                {
                    if (errno == ESRCH)
                    {
                        // Process no longer exists
                        printf("Process %d has terminated\n", rpid);
                        break;
                    }
                }
                usleep(1000); // 1ms
                receive_processes();
                // Allow scheduler to receive messages during polling    receive_processes();
                rpid = running_process != NULL ? rpid : -1;
            }
            while (rpid != -1);
        }
        else if (scheduler_type == SRTN)
        {
            running_process = srtn(min_heap_queue);
            if (running_process == NULL) continue; // there is no process to run
            int remaining_time = running_process->remaining_time;
            kill(running_process->pid,SIGCONT);
            int crt_time = get_clk();
            int old_time = crt_time;

            short has_received = 0;

            while (remaining_time > 0 && !has_received)
            {
                while ((crt_time = get_clk()) == old_time)
                {
                    usleep(1000);
                    if (receive_processes() == 0) has_received = 1;
                } // busy wait the scheduler for a clk

                remaining_time -= crt_time - old_time;
            }

            // If the child hasn't finished execution else it would be null
            if (running_process != NULL)
            {
                if (remaining_time > 0)
                {
                    kill(running_process->pid,SIGTSTP); // Pause the child for context switching
                    running_process->status = READY;
                    running_process->remaining_time -= crt_time - old_time;
                    running_process->last_run_time = crt_time;
                    min_heap_insert(min_heap_queue, running_process);
                }
            }
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
            kill(running_process->pid,SIGTSTP); // Pause the child for context switching
            running_process->status = READY;
            running_process->remaining_time -= time_to_run;
            running_process->last_run_time = crt_time;
            if (running_process->remaining_time)
                enqueue(rr_queue, running_process);
            else
            {
                running_process->status = TERMINATED;
                running_process->finish_time = crt_time;
                running_process->waiting_time = (running_process->finish_time - running_process->arrival_time) -
                    running_process->runtime;
                running_process->turnaround_time = running_process->finish_time - running_process->arrival_time;
                running_process->weighted_turnaround = (float)running_process->turnaround_time / running_process->
                    runtime;
                log_process_state(running_process, "finished", get_clk());
            }
        }
        else
        {
            fprintf(stderr, "Unknown scheduler_type: %d\n", scheduler_type);
            exit(EXIT_FAILURE);
        }
    }
    // Must Be called before the clock is destoryed !!!
    generate_statistics();
    destroy_clk(1);
    exit(0);
}

int receive_processes(void)
{
    if (msgid == -1)
        return -1;

    PCB received_pcb;
    size_t recv_val = msgrcv(msgid, &received_pcb, sizeof(PCB), 1, IPC_NOWAIT);

    if (recv_val == -1)
    {
        if (errno == ENOMSG)
            return errno; // No message available
        else if (errno == EIDRM || errno == EINVAL)
        {
            // EIDRM: Queue was removed
            // EINVAL: Invalid queue ID (queue no longer exists)
            // printf("Message queue has been closed or removed\n");
            return -2; // Special return value to indicate queue closure
        }
        else
        {
            perror("Error receiving message");
            return errno;
        }
    }

    while (recv_val != -1)
    {
        printf("Received process ID: %d, arrival time: %d, remaining_time: %d at %d\n",
               received_pcb.pid, received_pcb.arrival_time, received_pcb.remaining_time, get_clk());

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

        if (recv_val == -1 && (errno == EIDRM || errno == EINVAL))
        {
            printf("Message queue has been closed or removed during processing\n");
            return -2; // Queue was removed during processing
        }
    }

    return 0;
}

void scheduler_cleanup(int signum)
{
    printf("[SCHEDULER] scheduler_cleanup CALLED\n");

    if (log_file)
    {
        fclose(log_file);
        log_file = NULL;
    }

    // Cleanup memory resources if they still exist
    if (min_heap_queue)
    {
        // Free any remaining PCBs in the heap
        while (min_heap_queue->size > 0)
        {
            PCB* pcb = min_heap_extract_min(min_heap_queue);
            if (pcb)
                free(pcb);
        }
        free(min_heap_queue);
        min_heap_queue = NULL;
    }

    if (rr_queue)
    {
        // Free any remaining PCBs in the queue
        while (!isQueueEmpty(rr_queue))
        {
            PCB* pcb = dequeue(rr_queue);
            if (pcb)
                free(pcb);
        }
        free(rr_queue);
        rr_queue = NULL;
    }

    // Don't try to remove the message queue that's already been removed
    if (msgid != -1)
    {
        // Check if queue still exists
        struct msqid_ds queue_info;
        if (msgctl(msgid, IPC_STAT, &queue_info) != -1)
        {
            msgctl(msgid, IPC_RMID, NULL);
        }
        msgid = -1;
    }

    for (int i = 0; i < MAX_INPUT_PROCESSES; i++)
        if (finished_process_info[i] != NULL)
        {
            free(finished_process_info[i]);
            finished_process_info[i] = NULL;
        }

    if (finished_process_info != NULL)
    {
        free(finished_process_info);
        finished_process_info = NULL;
    }

    printf("[SCHEDULER] scheduler_cleanup FINISHED \n");

    if (signum != 0)
    {
        exit(signum);
    }
}

void child_cleanup()
{
    if (running_process == NULL) return;;

    sync_clk();
    signal(SIGCHLD, child_cleanup);
    printf("[SCHEDULER] CHILD_CLEANUP CALLED\n");

    if (running_process)
    {
        int current_time = get_clk();
        running_process->finish_time = current_time;
        log_process_state(running_process, "finished", current_time);
        if (finished_processes_count < MAX_INPUT_PROCESSES)
        {
            if (finished_process_info[finished_processes_count] == NULL)
            {
                finished_process_info[finished_processes_count] = (finishedProcessInfo*)malloc(
                    sizeof(finishedProcessInfo));
                if (!finished_process_info[finished_processes_count])
                {
                    perror("Failed to malloc finished_process_info");
                }
                else
                {
                    // Only access if malloc succeeded
                    finished_process_info[finished_processes_count]->ta = current_time - running_process
                        ->arrival_time;
                    finished_process_info[finished_processes_count]->wta = (float)(finished_process_info[
                        finished_processes_count]->ta) / running_process->runtime;
                    finished_process_info[finished_processes_count]->waiting_time = running_process->waiting_time;
                }
            }

            process_count--;
            finished_processes_count++;
        }
        else
        {
            printf("WARNING: Exceeded maximum number of processes!\n");
        }

        free(running_process);
        running_process = NULL;
    }
    else
    {
        printf("Requested to cleanup none????\n");
    }
    printf("[SCHEDULER] CHILD_CLEANUP FINISHED\n");
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

    finished_processes_count = 0;
    finished_process_info = (finishedProcessInfo**)malloc(MAX_INPUT_PROCESSES * sizeof(finishedProcessInfo*));

    if (!finished_process_info)
    {
        perror("Failed to allocate memory for PCB");
        return -1;
    }

    // Initialize all pointers to NULL
    for (int i = 0; i < MAX_INPUT_PROCESSES; i++)
        finished_process_info[i] = NULL;

    printf("Scheduler initialized successfully at time %d\n", current_time);
    return 0;
}
