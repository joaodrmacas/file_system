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


#define EXPECTED_INPUT (4)
#define MANAGER_LIST_START_SIZE (8)

int getResponse(uint8_t* response){
    int32_t return_code = 0;
    for(int i=4; i>=1; --i) {
        return_code <<= 8;
        return_code += response[i];
    }
    if (return_code<0){
        fprintf(stdout, "ERROR %s\n", &response[5]);
        return -1;
    }
    fprintf(stdout, "OK\n");
    return 0;
}

int compare_strings(const void* a, const void* b) {
    char* str1 = *(char**)a + 2;
    char* str2 = *(char**)b + 2;
    return strcmp(str1, str2);
}

int main(int argc, char **argv) {

    //Optamos por truncar todo o tipo de input de utilizador
    
    check_input_args(argc,EXPECTED_INPUT);

    char register_pipe[PIPE_LEN];
    char pipe_name[PIPE_LEN];
    char box_name[BOX_LEN];
    strncpy(register_pipe,argv[1],PIPE_LEN-1);
    strncpy(pipe_name,argv[2],PIPE_LEN-1);
    register_pipe[PIPE_LEN-1]=0;
    pipe_name[PIPE_LEN-1]=0;

    if (start_fifo(pipe_name)==-1){
        return -1;
    }


    if (strcmp(argv[3],"create")==0){
        strncpy(box_name,argv[4],BOX_LEN-1);
        box_name[BOX_LEN-1]=0;
        uint8_t register_message[MANAGER_CREATE_BUFFER_SIZE];
        memset(register_message,0,MANAGER_CREATE_BUFFER_SIZE);
        register_message[0] = 3;
        memcpy(register_message + 1, pipe_name, PIPE_LEN);
        memcpy(register_message + PIPE_LEN + 1, box_name, BOX_LEN);
        int tx = open(register_pipe, O_WRONLY);

        if (tx == -1) {
            fprintf(stderr, "open error: %s\n", strerror(errno));
            close(tx);
            exit(EXIT_FAILURE);
        }

        if (write(tx,register_message,MANAGER_CREATE_BUFFER_SIZE)<0){
            fprintf(stderr, "write error: %s\n", strerror(errno));
            close(tx);
            exit(EXIT_FAILURE);
        }
        close(tx);
    }
    else if (strcmp(argv[3],"remove")==0){
        strncpy(box_name,argv[4],BOX_LEN-1);
        box_name[BOX_LEN-1]=0;
        uint8_t remove_message[MANAGER_REMOVE_BUFFER_SIZE];
        memset(remove_message,0,MANAGER_REMOVE_BUFFER_SIZE);
        remove_message[0] = 5;
        memcpy(remove_message + 1, pipe_name, PIPE_LEN);
        memcpy(remove_message + PIPE_LEN + 1, box_name, BOX_LEN);

        int tx = open(register_pipe, O_WRONLY);
        if (tx == -1) {
            fprintf(stderr, "open error: %s\n", strerror(errno));
            close(tx);
            exit(EXIT_FAILURE);
        }

        if (write(tx,remove_message,MANAGER_REMOVE_BUFFER_SIZE)<0){
            fprintf(stderr, "write error: %s\n", strerror(errno));
            close(tx);
            exit(EXIT_FAILURE);
        }
        close(tx);
    }
    else if (strcmp(argv[3],"list")==0){

        uint8_t list_message[MANAGER_LIST_REQUEST_BUFFER_SIZE];
        memset(list_message,0,MANAGER_LIST_REQUEST_BUFFER_SIZE);
        list_message[0] = 7;
        memcpy(list_message + 1, pipe_name, PIPE_LEN);

        int tx = open(register_pipe, O_WRONLY);

        if (tx == -1) {
            fprintf(stderr, "open error: %s\n", strerror(errno));
            close(tx);
            exit(EXIT_FAILURE);
        }

        if (write(tx,list_message,MANAGER_LIST_REQUEST_BUFFER_SIZE)<0){
            fprintf(stderr, "write error: %s\n", strerror(errno));
            close(tx);
            exit(EXIT_FAILURE);
        }
        close(tx);
    }
    else if (strcmp(argv[3],"subs")==0){
        uint8_t listmessage[MANAGER_LIST_REQUEST_BUFFER_SIZE];
        memset(listmessage,0,MANAGER_LIST_REQUEST_BUFFER_SIZE);
        listmessage[0] = 11;
        memcpy(listmessage+1,pipe_name,PIPE_LEN);

        int tx = open(register_pipe, O_WRONLY);

        if (tx == -1) {
            fprintf(stderr, "open error: %s\n", strerror(errno));
            close(tx);
            exit(EXIT_FAILURE);
        }

        if (write(tx,listmessage,MANAGER_LIST_REQUEST_BUFFER_SIZE)<0){
            fprintf(stderr, "write error: %s\n", strerror(errno));
            close(tx);
            exit(EXIT_FAILURE);
        }
        close(tx);
    }
    else {
        fprintf(stderr,"error: invalid command for manager.\n");
        return -1;
    }

    uint8_t op_code = 0;
    int rx = open(pipe_name, O_RDONLY);
    if (rx == -1) {
        fprintf(stderr, "open error: %s\n", strerror(errno));
        close(rx);
        unlink(pipe_name);
        exit(EXIT_FAILURE);
    }
    //reads opcode;
    ssize_t readd = read(rx,&op_code,1);
    if (readd==-1){
        fprintf(stderr, "read error: %s\n", strerror(errno));
        close(rx);
        unlink(pipe_name);
        exit(EXIT_FAILURE);
    }
    if (op_code==4 || op_code==6){
        uint8_t response[MANAGER_RESPONSE_BUFFER_SIZE];
        memset(response,0,MANAGER_RESPONSE_BUFFER_SIZE);
        response[0]=op_code;
        //reads the rest of the message.
        ssize_t r = read(rx,&response[1],MANAGER_RESPONSE_BUFFER_SIZE-1);
        if (r==-1){
            fprintf(stderr, "read error: %s\n", strerror(errno));
            close(rx);
            unlink(pipe_name);
            exit(EXIT_FAILURE);
        }

        if(getResponse(response)==-1){
            close(rx);
            unlink(pipe_name);
            exit(EXIT_FAILURE);
        }
    }

    else if (op_code==8){
        uint8_t** list = (uint8_t**) malloc(MANAGER_LIST_START_SIZE*sizeof(uint8_t*));
        size_t list_capacity = MANAGER_LIST_START_SIZE,list_size=0;
        for (int i=0;i<MANAGER_LIST_START_SIZE;i++){
            list[i] = NULL;
        }
        uint8_t last=0;
        ssize_t r;
        for(int i=0;last==0;i++){
            list[i] = (uint8_t*) malloc(sizeof(uint8_t)*MANAGER_LIST_RESPONSE_BUFFER_SIZE);
            memset(list[i],0,MANAGER_LIST_RESPONSE_BUFFER_SIZE);
            if (i==0){
                r = read(rx,&list[i][1],MANAGER_LIST_RESPONSE_BUFFER_SIZE-1);
                list[i][0]=op_code;
                list_size++;
            }else{
                r = read(rx,list[i],MANAGER_LIST_RESPONSE_BUFFER_SIZE);
                if (list_size==list_capacity-1){
                    list_capacity*=2;
                    list =(uint8_t**) realloc(list,sizeof(uint8_t*)*list_capacity);
                    for (int j= (int) list_capacity/2;j<list_capacity;j++){
                        list[j]=NULL;
                    }
                }
                list_size++;
            }
            last = list[i][1];
            if (r==-1){
                fprintf(stderr, "read error: %s\n", strerror(errno));
                close(rx);
                unlink(pipe_name);
                exit(EXIT_FAILURE);
            }
        }
        qsort((void*)list,list_size,sizeof(uint8_t*),compare_strings);

        for (int i=0;i<list_size;i++){
            if (list[i][1]==1 && list[i][2]=='\0'){
                fprintf(stdout, "NO BOXES FOUND\n");
                close(rx);
                unlink(pipe_name);
                exit(EXIT_FAILURE);
            }
            fprintf(stdout, "%s %zu %zu %zu\n", &list[i][2],  *((uint64_t*) &list[i][2+BOX_LEN]),*((uint64_t*) &list[i][10+BOX_LEN]),*((uint64_t*) &list[i][18+BOX_LEN]));
            free(list[i]);
        }
        for (int i= (int) list_size;i<list_capacity;i++){
            free(list[i]);
        }
        free(list);

    }
    else if (op_code==12){
        uint8_t** list = (uint8_t**) malloc(MANAGER_LIST_START_SIZE*sizeof(uint8_t*));
        size_t list_capacity = MANAGER_LIST_START_SIZE,list_size=0;
        for (int i=0;i<MANAGER_LIST_START_SIZE;i++){
            list[i] = NULL;
        }
        uint8_t last=0;
        ssize_t r;

        for(int i=0;last==0;i++){
            list[i] = (uint8_t*) malloc(sizeof(uint8_t)*MANAGER_LIST_RESPONSE_BUFFER_SIZE);
            memset(list[i],0,MANAGER_LIST_RESPONSE_BUFFER_SIZE);
            if (i==0){
                r = read(rx,&list[i][1],MANAGER_LIST_RESPONSE_BUFFER_SIZE-1);
                list[i][0]=op_code;
                list_size++;
             }else{
                r = read(rx,list[i],MANAGER_LIST_RESPONSE_BUFFER_SIZE);
                if (list_size==list_capacity-1){
                    list_capacity*=2;
                    list =(uint8_t**) realloc(list,sizeof(uint8_t*)*list_capacity);
                    for (int j= (int) list_capacity/2;j<list_capacity;j++){
                        list[j]=NULL;
                    }
                }
                list_size++;
            }
            last = list[i][1];
            if (r==-1){
                fprintf(stderr, "read error: %s\n", strerror(errno));
                close(rx);
                unlink(pipe_name);
                exit(EXIT_FAILURE);
            }
        }

        for (int i=0;i<list_size;i++){
                if (list[i][1]==1 && list[i][2]=='\0'){
                    fprintf(stdout, "NO SUBS FOUND\n");
                    close(rx);
                    unlink(pipe_name);
                    exit(EXIT_FAILURE);
                }
                fprintf(stdout, "%s\n", &list[i][2]);
                free(list[i]);
            }
        for (int i= (int) list_size;i<list_capacity;i++){
            free(list[i]);
        }
        free(list);
    }

    close(rx);
    unlink(pipe_name);

    return -1;
}
