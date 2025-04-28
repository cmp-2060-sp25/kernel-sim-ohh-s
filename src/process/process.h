#pragma once

#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "clk.h"

// Add shared memory key definition
#define SHM_KEY 400
#define MAX_PROCESSES 100

void sigIntHandler(int signum);
void sigStpHandler(int signum);
void sigContHandler(int signum);
void run_process(int runtime);
int get_time_to_run(int shmid, pid_t pid);
