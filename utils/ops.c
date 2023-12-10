#include "ops.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int check_input_args(int argc,int expectedArgs){
    if (argc<expectedArgs){
        fprintf(stderr,"input error: missing parameters.\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int start_fifo(char* fifo){

    if (unlink(fifo) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", fifo ,strerror(errno));
        return -1;
    }

    

    if (mkfifo(fifo, 0640) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}