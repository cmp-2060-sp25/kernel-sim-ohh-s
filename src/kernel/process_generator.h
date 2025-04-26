#pragma once

processParameters** read_process_file(const char* filename, int* count);
void process_generator_cleanup(int signum);
extern int quantum;
void child_process_handler(int signum);
