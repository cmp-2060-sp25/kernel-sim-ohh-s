#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <bits/signum-arch.h>
#include "clk.h"
#include "pcb.h"
#include "queue.h"
#include "scheduler.h"
#include "headers.h"
#include "min_heap.h"
#include "process_generator.h"
extern int scheduler_type;

// compare function for priority queue
int compare_processes(const void* p1, const void* p2)
{
    PCB* process1 = (PCB*)p1;
    PCB* process2 = (PCB*)p2;
    if (scheduler_type == HPF)
    {
        if (process1->priority != process2->priority)
        {
            return process1->priority - process2->priority;
        }

        // If priorities are equal return that come first
        return process1->arrival_time - process2->arrival_time;
    }
    else // SRTN
    {
        if (process1->remaining_time != process2->remaining_time)
        {
            return process1->remaining_time - process2->remaining_time;
        }

        // If priorities are equal return that come first
        return process1->arrival_time - process2->arrival_time;
    }
}

// HPF algorithm
PCB* hpf(min_heap_t* ready_queue, int current_time)
{
    if (!min_heap_is_empty(ready_queue))
    {
        PCB* next_process = min_heap_extract_min(ready_queue);
        while (next_process->remaining_time == 0)
        {
            log_process_state(next_process, "started", current_time);
            log_process_state(next_process, "finished", current_time);
            if (min_heap_is_empty(ready_queue))
                return NULL;
            next_process = min_heap_extract_min(ready_queue);
        }
        next_process->status = RUNNING;
        next_process->waiting_time = current_time - next_process->arrival_time;
        // assuming that any process is initially having start time -1
        if (next_process->start_time == -1)
        {
            next_process->start_time = current_time;
        }
        log_process_state(next_process, "started", current_time);
        kill(next_process->pid,SIGCONT);
        return next_process;
    }
    return NULL;
}

PCB* srtn(min_heap_t* ready_queue, int current_time)
{
    if (!min_heap_is_empty(ready_queue))
    {
        PCB* next_process = min_heap_extract_min(ready_queue);
        next_process->status = RUNNING;
        if (next_process->last_run_time == -1)
        {
            next_process->waiting_time = current_time - next_process->arrival_time;
        }
        else next_process->waiting_time += current_time - next_process->last_run_time;

        // assuming that any process is initially having start time -1
        if (next_process->start_time == -1)
        {
            log_process_state(next_process, "started", current_time);
            next_process->start_time = current_time;
            next_process->response_time = next_process->start_time - next_process->arrival_time;
        }
        else
            log_process_state(next_process, "resumed", current_time);
        return next_process;
    }
    return NULL;
}


// RR algorithm
PCB* rr(Queue* ready_queue, int current_time)
{
    static int time_slice = 0; // Tracks the time slice for the current process

    if (running_process && running_process->remaining_time <= 0)
    {
        running_process->status = TERMINATED;
        running_process->finish_time = current_time;
        running_process->waiting_time = (running_process->finish_time - running_process->arrival_time) - running_process
            ->runtime;
        log_process_state(running_process, "finished", current_time);
        running_process = NULL;
        time_slice = 0; // Reset the time slice
    }

    // If the time slice for the running process has expired
    if (running_process && time_slice >= quantum)
    {
        running_process->status = READY;
        enqueue(ready_queue, running_process); // Re-enqueue the process
        log_process_state(running_process, "resumed", current_time);
        running_process = NULL;
        time_slice = 0; // Reset the time slice
    }

    // If no process is currently running and the ready queue is not empty
    if (!running_process && !isQueueEmpty(ready_queue))
    {
        running_process = dequeue(ready_queue);
        running_process->status = RUNNING;

        // If the process is starting for the first time
        if (running_process->start_time == -1)
        {
            log_process_state(running_process, "started", current_time);
            running_process->start_time = current_time;
        }
        else
            log_process_state(running_process, "resumed", current_time);
        time_slice = 0; // Reset the time slice for the new process
    }

    // Increment the time slice for the running process
    if (running_process)
    {
        time_slice++;
    }

    return running_process;
}


// Log process state changes
void log_process_state(PCB* process, char* state, int time)
{
    if (strcmp(state, "started") == 0)
    {
        fprintf(log_file, "At time %d process %d %s arr %d total %d remain %d wait %d\n",
                time, process->id, state, process->arrival_time, process->runtime,
                process->remaining_time, process->waiting_time);
    }
    else if (strcmp(state, "finished") == 0)
    {
        fprintf(log_file, "At time %d process %d %s arr %d total %d remain %d wait %d TA %d WTA %.2f\n",
                time, process->id, state, process->arrival_time, process->runtime,
                0, process->waiting_time,
                (time - process->arrival_time), // Turnaround time
                (float)(time - process->arrival_time) / process->runtime); // Weighted turnaround time
    }
    else
    {
        fprintf(log_file, "At time %d process %d %s arr %d total %d remain %d wait %d\n",
                time, process->id, state, process->arrival_time, process->runtime,
                process->remaining_time, process->waiting_time);
    }

    fflush(log_file);
}


// Generate performance statistics
void generate_statistics()
{
    if (process_count == 0) return;


    float total_wait = 0;
    float total_wta = 0;
    float total_ta = 0;
    float* wta_values = (float*)malloc(process_count * sizeof(float));

    int cpu_idle_time = 0;
    int total_execution_time = get_clk(); // Total simulation time

    for (int i = 0; i < process_count; i++)
    {
        // PCB* process = finished_processes[i];
        // int ta = process->finish_time - process->arrival_time;
        // float wta = (float)ta / process->runtime;
        //
        // total_wait += process->waiting_time;
        // total_ta += ta;
        // total_wta += wta;
        // wta_values[i] = wta;
    }

    float avg_wait = total_wait / process_count;
    float avg_wta = total_wta / process_count;

    // Calculate standard deviation for WTA
    float sum_squared_diff = 0;
    for (int i = 0; i < process_count; i++)
    {
        float diff = wta_values[i] - avg_wta;
        sum_squared_diff += diff * diff;
    }
    float std_wta = sqrt(sum_squared_diff / process_count);

    // Calculate CPU utilization (assuming idle time is properly tracked)
    float cpu_utilization = ((float)(total_execution_time - cpu_idle_time) / total_execution_time) * 100;

    // Write to performance file
    FILE* perf_file = fopen("scheduler.perf", "w");
    fprintf(perf_file, "CPU utilization = %.2f%%\n", cpu_utilization);
    fprintf(perf_file, "Avg WTA = %.2f\n", avg_wta);
    fprintf(perf_file, "Avg Waiting = %.2f\n", avg_wait);
    fprintf(perf_file, "Std WTA = %.2f\n", std_wta);
    fclose(perf_file);

    free(wta_values);
}
