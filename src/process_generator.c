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
#include "scheduler.h"


int main(int argc, char* argv[])
{
    int process_count;

    // Get List of processes
    process_parameters = read_process_file("processes.txt", &process_count);

    // Init IPC
    // Any file name
    key_t key = ftok("process_generator", 65);
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
    pid_t clk_pid = fork();

    if (clk_pid == 0)
    {
        signal(SIGINT, process_generator_cleanup);
        sync_clk();

        int remaining_processes = process_count;
        int crt_clk = get_clk();
        int old_clk = -1;
        while (remaining_processes > 0)
        {
            // Ensure that we move by increments of 1
            while ((crt_clk = get_clk()) - old_clk == 0)
                usleep(1); // 1us sleep;
            old_clk = crt_clk;

            printf("current time is %d\n", crt_clk);

            int messages_sent = 0;
            // Check the process_parameters[] for processes whose arrival time == crt_clk, and send them
            for (int i = 0; i < process_count; i++)
            {
                if (process_parameters[i] != NULL && process_parameters[i]->arrival_time == crt_clk)
                {
                    messages_sent++;
                    PCB proc_pcb = {
                        1, process_count - remaining_processes, process_parameters[i]->process_id,
                        process_parameters[i]->arrival_time, 0,
                        process_parameters[i]->runtime, process_parameters[i]->priority, 0, crt_clk, -1, -1, -1, -1, -1,
                        READY,
                    };
                    // Send the message
                    if (msgsnd(msgid, &proc_pcb, sizeof(PCB), 0) == -1)
                    {
                        perror("Error sending message");
                    }

                    // Cleanup
                    free(process_parameters[i]);
                    process_parameters[i] = NULL;
                    remaining_processes--;
                }
            }

            if (messages_sent > 0)
            {
                printf("Sent %d message(s) to scheduler\n", messages_sent);
            }
        }

        printf("All processes have been sent, exiting...\n");
        process_generator_cleanup(0);
        exit(0);
    }
    else
    {
        pid_t scheduler_pid = fork();
        if (scheduler_pid == 0)
            run_scheduler();
        else
        {
            init_clk();
            sync_clk();
            run_clk();
        }
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
        perror("Error opening file");
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
        int id, arrival, runtime, priority;
        if (sscanf(line, "%d\t%d\t%d\t%d", &id, &arrival, &runtime, &priority) == 4)
        {
            // Allocate memory for each ProcessMessage
            process_messages[index] = (processParameters*)malloc(sizeof(processParameters));

            // Set values
            process_messages[index]->mtype = 1; // Default message type
            process_messages[index]->process_id = id;
            process_messages[index]->arrival_time = arrival;
            process_messages[index]->runtime = runtime;
            process_messages[index]->priority = priority;

            index++;
        }
    }

    fclose(file);

    printf("Read %d processes from file\n", index);

    return process_messages;
}


void process_generator_cleanup(int signum)
{
    signal(signum, process_generator_cleanup);
    // Free each ProcessMessage
    for (int i = 0; i < process_count; i++)
    {
        if (process_parameters[i] != NULL)
        {
            free(process_parameters[i]);
            process_parameters[i] = NULL;
        }
    }

    // Free the array of pointers
    free(process_parameters);

    // Remove message queue
    if (msgid != -1)
    {
        msgctl(msgid, IPC_RMID, NULL);
    }

    printf("[PROC_GENERATOR] Resources cleaned up\n");

    if (signum != 0)
    {
        exit(signum);
    }
}
