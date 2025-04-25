#pragma once
#include "headers.h"
processParameters** read_process_file(const char* filename, int* count);
void process_generator_cleanup(int signum);
processParameters** process_parameters;
int msgid;
