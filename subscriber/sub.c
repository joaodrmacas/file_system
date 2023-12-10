#include "logging.h"
#include "ops.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>

#define EXPECTED_INPUT (4)

int messages_read=0;
char pipe_name[PIPE_LEN];
int rx = 0;

void sigint_handler(){
    if (close(rx)<0){
        fprintf(stderr,"close error: failed to close pipe.\n");
        exit(EXIT_FAILURE);
    }
    if (unlink(pipe_name) != 0 && errno != ENOENT) {
        fprintf(stderr,"unlink error: failed to delete pipe\n");
        exit(EXIT_FAILURE);
    }
    printf("total messages read: %d\n",messages_read);
    exit(EXIT_SUCCESS);
}

int getResponse(char* pipename){
    rx = open(pipename,O_RDONLY);
    if (rx==-1){
        fprintf(stderr,"open error: could not open file\n");
        return -1;
    }

    uint8_t buffer[SUBSCRIBER_BUFFER_SIZE];
    memset(buffer,0,SUBSCRIBER_BUFFER_SIZE);


    while (true){
        ssize_t bytes_read = read(rx,buffer,SUBSCRIBER_BUFFER_SIZE);
        if (bytes_read<=0){
            fprintf(stderr,"error connecting to the server.\n");
            return -1;
        }

        if (buffer[0]!= 10){
            fprintf(stderr,"unrecognized request error.\n");
            return -1;
        }

        fprintf(stdout,"%s\n",&buffer[1]);
        messages_read++;
    }


}


int main(int argc, char **argv) {

    struct sigaction sa;
    sa.sa_handler = sigint_handler;

    sigaction(SIGINT,&sa,NULL);

    check_input_args(argc,EXPECTED_INPUT);

    char register_pipe[PIPE_LEN];
    char box_name[BOX_LEN];

    strncpy(register_pipe,argv[1],PIPE_LEN-1);
    register_pipe[PIPE_LEN-1]=0;
    strncpy(pipe_name,argv[2],PIPE_LEN-1);
    pipe_name[PIPE_LEN-1]=0;
    strncpy(box_name,argv[3],BOX_LEN-1);
    box_name[BOX_LEN-1]=0;

    if (mkfifo(pipe_name, 0640) != 0) {
        fprintf(stderr, "mkfifo error: %s\n", strerror(errno));
        return -1;
    }

    uint8_t register_message[REGISTRY_BUFFER_SIZE];
    memset(register_message,0,REGISTRY_BUFFER_SIZE);
    register_message[0] = 2;
    memcpy(register_message + 1, pipe_name, PIPE_LEN);
    memcpy(register_message + PIPE_LEN + 1, box_name, BOX_LEN);

    int tx = open(register_pipe, O_WRONLY);

    if (tx == -1) {
        fprintf(stderr, "open error: %s\n", strerror(errno));
        close(tx);
        exit(EXIT_FAILURE);
    }

    if (write(tx,register_message,REGISTRY_BUFFER_SIZE)<0){
        fprintf(stderr, "write error: %s\n", strerror(errno));
        close(tx);
        exit(EXIT_FAILURE);
    }

    getResponse(pipe_name);

    close(tx);
    unlink(pipe_name);

}
