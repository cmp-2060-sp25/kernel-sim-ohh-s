#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>    // for fork, execl
#include <sys/ipc.h>
#include <sys/types.h> // for pid_t
#include <sys/msg.h>
#include "clk.h"
#include "headers.h"
#include "process_generator.h"
#include <string.h>
#include <sys/wait.h>
#include "colors.h"
#include "memory_manager.h"

#include "scheduler.h"
#include <bits/getopt_core.h>
int scheduler_type = -1; // Default invalid value
char* process_file = "processes.txt"; // Default filename
int quantum = 2; // Default quantum value
processParameters** process_parameters;
int msgid;
key_t key;
int remaining_processes;

// Memory size for the buddy system (adjust as needed)
#define MEMORY_SIZE 1024

// Function to check and process waiting list
void process_waiting_list() {
    while (mm_has_waiting_processes()) {
        processParameters* proc = mm_get_next_allocatable_process();
        if (!proc) break;
        if (DEBUG) printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] Processing waiting process ID %d\n" ANSI_COLOR_RESET, proc->id);
        
        // Attempt to allocate memory first - with a temporary ID
        int allocation = mm_allocate(proc->id, proc->memsize);
        if (allocation == -1) {
            // If allocation failed, put back in the waiting list
            if (DEBUG) printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] Still can't allocate memory for process ID %d, keeping in waiting list\n" ANSI_COLOR_RESET, proc->id);
            mm_add_to_waiting_list(proc);
            free(proc);
            break; // Break from the loop since memory is still constrained
        }
        
        // Memory allocation succeeded, now fork
        pid_t pid = fork();
        if (pid == 0) {
            char runtime_str[16];
            snprintf(runtime_str, sizeof(runtime_str), "%d", proc->runtime);
            char pid_str[16];
            snprintf(pid_str, sizeof(pid_str), "%d", getpid());
            execl("./process", "process", runtime_str, pid_str, (char*)NULL);
            perror("execl failed");
            exit(1);
        } else if (pid > 0) {
            proc->pid = pid;
            // Establish bidirectional mapping between PID and process ID
            mm_map_pid_to_id(pid, proc->id);
            
            if (allocation != -1) {
                if (DEBUG)
                    printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] Started waiting process ID %d with PID %d\n" ANSI_COLOR_RESET, proc->id, pid);
                PCB proc_pcb = {
                    1, proc->id, pid,
                    proc->arrival_time, proc->runtime,
                    proc->runtime, proc->priority, 0, -1, -1, -1, -1, -1,
                    -1,
                    READY,
                };
                if (msgsnd(msgid, &proc_pcb, sizeof(PCB), 0) == -1) {
                    if (DEBUG)
                        perror("Error sending message");
                }
            }
        } else {
            perror("fork failed");
            // Free the allocation since forking failed
            mm_free_by_id(proc->id);
        }
        free(proc);
    }
}

int main(int argc, char* argv[])
{
    int process_count;

    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "s:f:q:")) != -1)
    {
        switch (opt)
        {
        case 's':
            if (strcmp(optarg, "rr") == 0)
            {
                scheduler_type = 0; // RR
            }
            else if (strcmp(optarg, "hpf") == 0)
            {
                scheduler_type = 1; // HPF
            }
            else if (strcmp(optarg, "srtn") == 0)
            {
                scheduler_type = 2; // SRTN
            }
            else
            {
                fprintf(stderr, "Invalid scheduler type: %s\n", optarg);
                fprintf(stderr, "Valid options are: rr, hpf, srtn\n");
                exit(EXIT_FAILURE);
            }
            printf(ANSI_COLOR_MAGENTA"[MAIN] Using scheduler: %s\n"ANSI_COLOR_RESET, optarg);
            break;
        case 'f':
            process_file = optarg;
            printf(ANSI_COLOR_MAGENTA"[MAIN] Reading processes from: %s\n"ANSI_COLOR_RESET, process_file);
            break;
        case 'q':
            quantum = atoi(optarg);
            printf(ANSI_COLOR_MAGENTA"[MAIN] Quantum set to: %d\n"ANSI_COLOR_RESET, quantum);
            break;
        default:
            fprintf(stderr, "Usage: %s -s <scheduling-algorithm> -f <processes-text-file> [-q <quantum>]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // Check if scheduler type is provided
    if (scheduler_type == -1)
    {
        fprintf(stderr, "Scheduler type must be specified with -s option\n");
        fprintf(stderr, "Usage: %s -s <scheduling-algorithm> -f <processes-text-file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Get List of processes
    process_parameters = read_process_file(process_file, &process_count);
    remaining_processes = process_count;

    // Initialize memory manager
    if (!mm_init(MEMORY_SIZE)) {
        fprintf(stderr, "Failed to initialize memory manager\n");
        exit(EXIT_FAILURE);
    }
    
    if (DEBUG) {
        printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] Memory manager initialized with size %d\n"ANSI_COLOR_RESET, 
            MEMORY_SIZE);
    }

    // Init IPC
    // Any file name
    key = ftok("process_generator", 65);
    msgid = msgget(key, 0666 | IPC_CREAT);
    if (msgid == -1)
    {
        perror("Error creating message queue");
        exit(1);
    }

    /*
     * Fork -> sends the processes at the appropriate time to the scheduler
     * Parent -> runs the clk
     */
    pid_t process_generator_pid = getpid();
    pid_t clk_pid = fork();
    // Child b
    // Child -> Fork processes and sends their pcb to the scheduler at the appropriate time
    if (clk_pid == 0)
    {
        // Child -> run_scheduler
        pid_t scheduler_pid = fork();
        if (scheduler_pid == 0)
        {
            signal(SIGINT, process_generator_cleanup);
            signal(SIGCHLD, child_process_handler);
            sync_clk();

            int remaining_processes = process_count;
            int crt_clk = get_clk();
            int old_clk = -1;
            
            // Continue running until all processes are processed and waiting list is empty
            while (remaining_processes > 0 || mm_has_waiting_processes())
            {
                // Ensure that we move by increments of 1
                while ((crt_clk = get_clk()) - old_clk == 0);
                old_clk = crt_clk;

                // First, try to process waiting list
                process_waiting_list();
                
                int messages_sent = 0;
                
                // Check the process_parameters[] for processes whose arrival time == crt_clk
                for (int i = 0; i < process_count; i++)
                {
                    if (process_parameters[i] != NULL && process_parameters[i]->arrival_time == crt_clk)
                    {
                        // Try to allocate memory first - with process ID as identifier
                        int allocation = mm_allocate(process_parameters[i]->id, process_parameters[i]->memsize);
                        
                        if (allocation == -1) {
                            // Memory allocation failed, add to waiting list
                            if (DEBUG)
                                printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] Cannot allocate memory for process ID %d, adding to waiting list\n" ANSI_COLOR_RESET, process_parameters[i]->id);
                            mm_add_to_waiting_list(process_parameters[i]);
                            continue;
                        }
                        
                        // Memory allocation succeeded, now fork
                        pid_t pid = fork();
                        if (pid == 0)
                        {
                            char runtime_str[16];
                            snprintf(runtime_str, sizeof(runtime_str), "%d", process_parameters[i]->runtime);
                            char pid_str[16];
                            snprintf(pid_str, sizeof(pid_str), "%d", process_generator_pid);
                            execl("./process", "process", runtime_str, pid_str, (char*)NULL);
                            perror("execl failed");
                            exit(1);
                        }
                        else if (pid > 0)
                        {
                            process_parameters[i]->pid = pid;
                            // Add mapping between PID and process ID
                            mm_map_pid_to_id(pid, process_parameters[i]->id);
                            
                            if (allocation != -1)
                            {
                                if (DEBUG)
                                    printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] Allocated memory at offset %d for PID %d\n" ANSI_COLOR_RESET, allocation, pid);
                                // Send message to scheduler
                                messages_sent++;
                                PCB proc_pcb = {
                                    1, process_parameters[i]->id, pid,
                                    process_parameters[i]->arrival_time, process_parameters[i]->runtime,
                                    process_parameters[i]->runtime, process_parameters[i]->priority, 0, -1, -1, -1, -1, -1,
                                    -1,
                                    READY,
                                };
                                if (msgsnd(msgid, &proc_pcb, sizeof(PCB), 0) == -1)
                                {
                                    if (DEBUG)
                                        perror("Error sending message");
                                }
                            }
    
                        }
                        else
                        {
                            perror("fork failed");
                            // Free the allocation since forking failed
                            mm_free_by_id(process_parameters[i]->id);
                        }
                    }
                    else if (process_parameters[i] != NULL && process_parameters[i]->arrival_time > crt_clk)
                        break;
                }

                if (messages_sent > 0 && DEBUG)
                    printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] Sent %d message(s) to scheduler\n"ANSI_COLOR_RESET, messages_sent);
                
                // Check if we have waiting processes that might now be allocatable
                if (messages_sent > 0) {
                    process_waiting_list();
                }
                
                if (DEBUG && remaining_processes == 0 && mm_has_waiting_processes()) {
                    printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] All processes arrived, %d waiting for memory\n"
                        ANSI_COLOR_RESET, mm_get_waiting_count());
                }
                
                // If all processes have been read from the file but some are still waiting,
                // we need to keep trying until memory becomes available
                if (remaining_processes == 0 && mm_has_waiting_processes()) {
                    // Small delay to avoid busy waiting
                    usleep(50000); // 50ms
                }
            }

            if (DEBUG)
                printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] All processes have been sent, exiting...\n"ANSI_COLOR_RESET);
            
            // Final check - verify the waiting list is truly empty
            if (mm_has_waiting_processes()) {
                printf(ANSI_COLOR_RED"[PROC_GENERATOR] Warning: Exiting with %d processes still in waiting list!\n"
                    ANSI_COLOR_RESET, mm_get_waiting_count());
            } else {
                printf(ANSI_COLOR_MAGENTA"[PROC_GENERATOR] Waiting list is empty, all processes have been handled.\n"
                    ANSI_COLOR_RESET);
            }
            
            
            process_generator_cleanup(process_generator_pid);
            // Make the process generator wait until all children exited
            while (wait(NULL) > 0);
            // Clean up memory manager
            //mm_destroy();
            exit(0);
        }
        else
        {
            // Parent
            init_clk();
            sync_clk();
            run_clk();
        }
    }
    else
    {
        if (DEBUG)
            printf(ANSI_COLOR_MAGENTA"[MAIN] Running Scheduler with pid: %d\n"ANSI_COLOR_RESET, getpid());
        run_scheduler();
    }

    return 0;
}

/*
 * Reads the input file and returns a ProcessMessage**, a pointer to an
 * array of ProcessMessage, of size MAX_INPUT_PROCESSES
 */
processParameters** read_process_file(const char* filename, int* count)
{
    FILE* file = fopen(filename, "r");

    if (!file)
    {
        perror(ANSI_COLOR_MAGENTA"[MAIN] Error opening file"ANSI_COLOR_RESET);
        exit(1);
    }

    // Count lines to allocate memory
    char line[100];
    int line_count = 0;

    while (fgets(line, sizeof(line), file))
    {
        // Skip comment lines that start with #
        if (line[0] != '#' && line[0] != '\n')
        {
            line_count++;
        }
    }

    // Allocate memory for process message pointers
    /// Assuming a maximum of MAX_INPUT_PROCESSES processes
    processParameters** process_messages = (processParameters**)
        malloc(MAX_INPUT_PROCESSES * sizeof(processParameters*));

    // Initialize all pointers to NULL
    for (int i = 0; i < MAX_INPUT_PROCESSES; i++)
    {
        process_messages[i] = NULL;
    }

    *count = line_count;

    // Reset file pointer to beginning
    rewind(file);

    // Read process data
    int index = 0;

    while (fgets(line, sizeof(line), file))
    {
        // Skip comment lines that start with #
        if (line[0] == '#' || line[0] == '\n')
        {
            continue;
        }

        // Parse process information
        int id, arrival, runtime, priority, memsize = 0;
        int fields_read;
        
        // Try to parse with memory size field first (Phase 2 format)
        fields_read = sscanf(line, "%d\t%d\t%d\t%d\t%d", &id, &arrival, &runtime, &priority, &memsize);
        
        // If we couldn't read 5 fields, try the Phase 1 format (without memory size)
        if (fields_read < 5) {
            fields_read = sscanf(line, "%d\t%d\t%d\t%d", &id, &arrival, &runtime, &priority);
            
            // If Phase 1 format worked, use runtime as memsize (for backward compatibility)
            if (fields_read == 4) {
                memsize = runtime;
            } else {
                // Invalid format
                continue;
            }
        }

        // Allocate memory for each ProcessMessage
        process_messages[index] = (processParameters*)malloc(sizeof(processParameters));

        // Set values
        process_messages[index]->mtype = 1; // Default message type
        process_messages[index]->id = id;
        process_messages[index]->arrival_time = arrival;
        process_messages[index]->runtime = runtime;
        process_messages[index]->priority = priority;
        process_messages[index]->memsize = memsize;  // Store memory size

        index++;
    }

    fclose(file);

    if (DEBUG)
        printf(ANSI_COLOR_BLUE"[PROC_GENERATOR] Read %d processes from file\n"ANSI_COLOR_RESET, index);

    return process_messages;
}

void child_process_handler(int signum)
{
    signal(SIGCHLD, child_process_handler);
    int status;
    pid_t pid;
    pid = waitpid(-1, &status, WNOHANG);
    
    if (pid <= 0) return; // No child or error, just return
    
    if (DEBUG)
        printf(ANSI_COLOR_BLUE"[PROC_GENERATOR] Child process PID: %d has terminated with status %d\n"ANSI_COLOR_RESET,
               pid, WEXITSTATUS(status));
                
    // Check if this PID has memory allocated before freeing
    // This ensures we don't try to free memory that wasn't allocated
    // or was already freed
    int offset = -1;
    size_t size = 0;
    
    if (mm_check_pid_allocation(pid, &offset, &size)) {
        // Free memory when process terminates
        mm_free(pid);
        
        if (DEBUG)
            printf(ANSI_COLOR_BLUE"[PROC_GENERATOR] Released memory for terminated process PID: %d (offset: %d, size: %zu)\n"ANSI_COLOR_RESET,
                pid, offset, size);
    } else {
        if (DEBUG)
            printf(ANSI_COLOR_YELLOW"[PROC_GENERATOR] Process PID: %d had no memory allocated or was already freed\n"ANSI_COLOR_RESET,
                pid);
    }
}

void process_generator_cleanup(int scheduler_pid)
{
    // Add static flag to prevent double execution
    static int cleanup_in_progress = 0;

    // Guard against recursive calls
    if (cleanup_in_progress) return;
    cleanup_in_progress = 1;

    if (process_parameters != NULL)
    {
        // Free each ProcessMessage
        for (int i = 0; i < 100; i++)
        {
            if (process_parameters[i] != NULL)
            {
                free(process_parameters[i]);
                process_parameters[i] = NULL;
            }
        }
        free(process_parameters);
        process_parameters = NULL; // Ensure this is set to NULL
    }

    // Wait until message queue is empty before removing it
    if (msgid != -1)
    {
        struct msqid_ds queue_info;

        // Check if there are messages in the queue
        while (1)
        {
            if (msgctl(msgid, IPC_STAT, &queue_info) == -1)
            {
                if (DEBUG)
                    perror("Error getting message queue stats");
                break;
            }

            // If no messages are left in the queue, we can safely remove it
            if (queue_info.msg_qnum == 0)
            {
                if (DEBUG)
                    printf(ANSI_COLOR_BLUE"[PROC_GENERATOR] Message queue is empty, removing it\n"ANSI_COLOR_RESET);
                break;
            }

            if (DEBUG)
                printf(
                    ANSI_COLOR_BLUE"[PROC_GENERATOR] Waiting for queue to empty: %ld messages remaining\n"
                    ANSI_COLOR_RESET,
                    queue_info.msg_qnum);
            sleep(1);
        }

        // remove the message queue if still exists
        struct msqid_ds queue;
        if (msgctl(msgid, IPC_STAT, &queue) != -1)
        {
            msgctl(msgid, IPC_RMID, NULL);
            msgid = -1;
            if (DEBUG)
                printf(ANSI_COLOR_BLUE"[PROC_GENERATOR] Message queue removed successfully\n"ANSI_COLOR_RESET);
        }
    }

    // Wait only for the scheduler to exit
    int status;
    waitpid(scheduler_pid, &status, 0);
    if (DEBUG)
        printf(ANSI_COLOR_BLUE"[PROC_GENERATOR] Scheduler process %d exited with status %d\n"ANSI_COLOR_RESET,
               scheduler_pid, WEXITSTATUS(status));
    if (DEBUG)
        printf(ANSI_COLOR_BLUE"[PROC_GENERATOR] Resources cleaned up\n"ANSI_COLOR_RESET);

    signal(SIGINT,NULL);
}
