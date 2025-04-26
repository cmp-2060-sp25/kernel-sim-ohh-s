#pragma once

// Message structure for IPC
typedef struct
{
    long mtype;
    int id;
    int pid;
    int arrival_time;
    int runtime;
    int priority;
} processParameters;

#define MAX_INPUT_PROCESSES 100


// Constants
#define READY 0
#define RUNNING 1
#define TERMINATED 2 // I Think using this is wrong

// Extra States (IDK if we'll use them)
#define BLOCKED 2
#define PAUSED 3

// Scheduling algorithms
#define HPF 1
#define SRTN 2
#define RR 3

// Message types
#define PROCESS_ARRIVED 1
#define PROCESS_FINISHED 2
