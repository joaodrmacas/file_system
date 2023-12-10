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

#define EXPECTED_INPUT (4)
#define REQUEST_SIZE (1025)


//a partir do momento que le a mensagem de resposta do server, este comeca a escrever para o stdin e envia as mensagens.
int post_request(int tx){
    char input[MESSAGE_LEN];
    memset(input,0,MESSAGE_LEN);
    uint8_t request[REQUEST_SIZE];
    memset(request,0,REQUEST_SIZE);
    request[0] = 9;
    while (fgets(input,MESSAGE_LEN,stdin) != NULL){
        size_t len = strlen(input);
        if (input[len-1]=='\n'){
            input[len-1]='\0';
        }
        memcpy(request+1,input,MESSAGE_LEN-1);
        if(write(tx,request,REQUEST_SIZE)<0){
            if(errno == EPIPE)
                return 0;
            fprintf(stderr, "write error: %s\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}

int main(int argc, char **argv) {

    signal(SIGPIPE, SIG_IGN);

    check_input_args(argc,EXPECTED_INPUT);

    char register_pipe[PIPE_LEN];
    char pipe_name[PIPE_LEN];
    char box_name[BOX_LEN];

    strncpy(register_pipe,argv[1],PIPE_LEN-1);
    register_pipe[PIPE_LEN-1]=0;
    strncpy(pipe_name,argv[2],PIPE_LEN-1);
    pipe_name[PIPE_LEN-1]=0;
    strncpy(box_name,argv[3],BOX_LEN-1);
    box_name[BOX_LEN-1]=0;

    if (start_fifo(pipe_name)==-1)
        exit(EXIT_FAILURE);

    uint8_t register_message[REGISTRY_BUFFER_SIZE];
    memset(register_message,0,REGISTRY_BUFFER_SIZE);
    register_message[0] = 1;
    memcpy(register_message + 1, pipe_name, PIPE_LEN);
    memcpy(register_message + PIPE_LEN + 1, box_name, BOX_LEN);


    int tx = open(register_pipe, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "open error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (write(tx,register_message,REGISTRY_BUFFER_SIZE)<0){
        fprintf(stderr, "write error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    close(tx);

    tx = open(pipe_name,O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    post_request(tx);

    close(tx);
    unlink(pipe_name);
}
