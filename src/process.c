#include "clk.h"
#include "headers.h"
#include <signals.h>
#include <stdlib.h>
/* Modify this file as needed*/

int *RT; ///remaining process time
int shmid; ///shared memory id

void sigTermHandler(int signum) {
    exit(getpid());
}

void run_process(int runtime)
{
    sync_clk();

    //TODO: Keep running the process till its runtime is over

    destroy_clk(0);
}

int main(int agrc, char *argv[]) {
    signal(SIGTERM, sigTermHandler);

}
