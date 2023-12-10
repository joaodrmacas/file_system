
#include "logging.h"
#include "boxes.h"
#include "ops.h"
#include "producer-consumer.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <signal.h>

#define EXPECTED_INPUT (3)

//Duvidas:


/*
    ANTES DE STARTAR UM CLIENTE, SO DA UNLINK A FIFO SE NAO EXISTIR JÁ UMA.
    TRUNCAR TODOS OS INPUTS AO TAMANHO MAXIMO
*/


box_table_t box_table;
pc_queue_t req_queue;

int receiver_end=0;
char register_pipe_name[PIPE_LEN];


void close_server(){
    if (close(receiver_end)<0){
        fprintf(stderr,"close error: couldn't close register fifo.\n");
        exit(EXIT_FAILURE);
    }

    if (unlink(register_pipe_name)!=0 && errno != ENOENT){
        fprintf(stderr,"unlink error: couldn't unlink register fifo.\n");
        exit(EXIT_FAILURE);
    }
    
    if (tfs_destroy()==-1){
        fprintf(stderr,"tfs_destroy error: couldn't destroy tfs.\n");
        exit(EXIT_FAILURE);
    }

    box_table_destroy(&box_table);

    pcq_destroy(&req_queue);

    exit(EXIT_SUCCESS);
}

void sigpipe_handler(){
    printf("sigpipe detected.\n");
    return;
}

int manager_create_box(uint8_t* request){
    uint8_t response_type = 4;

    int tx = open((char*) &request[1], O_WRONLY);
    if (tx==-1){
        fprintf(stderr,"open error %s\n", strerror(errno));
        close(tx);
        return -1;
    }


    uint8_t response[MANAGER_RESPONSE_BUFFER_SIZE];
    memset(response,0,MANAGER_RESPONSE_BUFFER_SIZE);
    int32_t return_code = 0;
    char error_msg[MESSAGE_LEN];
    memset(error_msg,0,MESSAGE_LEN);
    char name[BOX_LEN+1];
    memset(name,0,BOX_LEN+1);
    
    
    if (box_find(&box_table,(char*) &request[257])!=NULL){
        return_code = -1;
        strcpy(error_msg,"box_find error: there's a box with that name already.");
        memcpy(response,&response_type,1);
        memcpy(response+1,&return_code,sizeof(int));
        memcpy(response+4+1,error_msg,sizeof(error_msg));
        
        if (write(tx,response,MANAGER_RESPONSE_BUFFER_SIZE)<0){
            fprintf(stderr,"write error: %s.\n", strerror(errno));
            close(tx);
            return -1;
        }

        close(tx);
        return -1;
    }

    name[0]='/';
    strcpy(name+1,(char*) &request[257]);

    int fd = tfs_open(name,TFS_O_CREAT);
    if (fd==-1){
        strcpy(error_msg,"tfs_open error: could not open the box file.");
        return_code = -1;
    }

    else if (tfs_close(fd)==-1){
        strcpy(error_msg,"tfs_close error: couldn't close the box file.");
        return_code = -1;
    }

    else if (box_add(&name[1],&box_table)==-1){
        strcpy(error_msg,"box_add error: could not add the box.");
        return_code = -1;
    }

    //copy all information to response buffer
    memcpy(response,&response_type,1);
    memcpy(response+1,&return_code,sizeof(int));
    memcpy(response+4+1,error_msg,sizeof(error_msg));


    if (write(tx,response,MANAGER_RESPONSE_BUFFER_SIZE)<0){
        fprintf(stderr,"write error: %s.\n", strerror(errno));
        close(tx);
        return -1;
    }

    return return_code;
}

int manager_remove_box(uint8_t* request){
    uint8_t response_type = 6;

    int tx = open((char*) &request[1], O_WRONLY);
    if (tx==-1){
        fprintf(stderr,"open error %s\n", strerror(errno));
        return -1;
    }

    uint8_t response[MANAGER_RESPONSE_BUFFER_SIZE];
    memset(response,0,MANAGER_RESPONSE_BUFFER_SIZE);
    int32_t return_code = 0;
    char error_msg[MESSAGE_LEN];
    memset(error_msg,0,MESSAGE_LEN);
    char name[BOX_LEN+1];
    memset(name,0,BOX_LEN+1);
    name[0]='/';
    strcpy(name+1,(char*) &request[257]);

    box_t* box = NULL;
    if ( (box = box_find(&box_table,(char*) &request[257]))==NULL){
        return_code = -1;
        strcpy(error_msg,"box_find error: box does not exist.");
    }

    if (return_code != -1){
        if (pthread_mutex_lock(&box->mutex) != 0){
            fprintf(stderr, "mutex_lock error: couldnt get the lock.\n");
            return -1;
        }
        if (box->pub_num > 0 || box->sub_num>0){
            fprintf(stderr, "can't remove active boxes.\n");
            if (pthread_mutex_unlock(&box->mutex)!=0){
                fprintf(stderr,"mutex_unlock error: couldnt leave the lock.\n");
                return -1;
            }
            return 0;
        }

        if (pthread_mutex_unlock(&box->mutex)!=0){
            fprintf(stderr,"mutex_unlock error: couldnt leave the lock.\n");
            return -1;
        }

        if ( tfs_unlink(name) == -1){
            return_code = -1;
            strcpy(error_msg,"tfs_unlink error: could not delete the box file.");
        }

        if (return_code != -1){
            if (box_remove(&name[1],&box_table) == -1){
                return_code = -1;
                strcpy(error_msg,"box_remove error: could not remove the box.");
            }
        }

    }

    memcpy(response,&response_type,1);
    memcpy(response+1,&return_code,sizeof(int));
    memcpy(response+4+1,error_msg,sizeof(error_msg));

    if (write(tx,response,MANAGER_RESPONSE_BUFFER_SIZE)<0){
        fprintf(stderr,"write error: %s.\n", strerror(errno));
        close(tx);
        return -1;
    }

    return return_code;
}

int manager_list_box(uint8_t* request){
    int tx = open((char*) &request[1], O_WRONLY);
    if (tx==-1){
        fprintf(stderr,"open error %s\n", strerror(errno));
        return -1;
    }

    uint8_t response[MANAGER_LIST_RESPONSE_BUFFER_SIZE];
    memset(response,0,MANAGER_LIST_RESPONSE_BUFFER_SIZE);
    uint8_t response_type = 8, last = 0;

    if (pthread_mutex_lock(&box_table.mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not get the lock.\n");
        return -1;
    }
    if (box_table.num_elem==0){
        response[0]=response_type;
        response[1]=1;

        if (write(tx,response,MANAGER_LIST_RESPONSE_BUFFER_SIZE)<0){
            fprintf(stderr,"write error: %s.\n", strerror(errno));
            close(tx);
            if (pthread_mutex_unlock(&box_table.mutex) != 0){
                fprintf(stderr,"mutex_unlock error: could not leave the lock.\n");
                return -1;
            }
            return -1;
        }
        if (pthread_mutex_unlock(&box_table.mutex) != 0){
            fprintf(stderr,"mutex_unlock error: could not leave the lock.\n");
            return -1;
        }
        return 0;
    }

    for (int i=0;i<box_table.size;i++){
        if (box_table.boxes[i] != NULL){
            memset(response,0,MANAGER_LIST_RESPONSE_BUFFER_SIZE);
            response[0] = response_type;
            if (i==box_table.size-1)
                last=1;
            response[1]=last;
            if (pthread_mutex_lock(&box_table.boxes[i]->mutex) != 0){
                fprintf(stderr,"mutex_lock error: could not get the lock.\n");
                return -1;
            }
            memcpy(response+2,box_table.boxes[i]->file_name,BOX_LEN);
            memcpy(response+2+BOX_LEN,&box_table.boxes[i]->size,sizeof(uint64_t));
            memcpy(response+10+BOX_LEN,&box_table.boxes[i]->pub_num,sizeof(uint64_t));
            memcpy(response+18+BOX_LEN,&box_table.boxes[i]->sub_num,sizeof(uint64_t));

            if (pthread_mutex_unlock(&box_table.boxes[i]->mutex) != 0){
                fprintf(stderr,"mutex_unlock error: could not leave the lock.\n");
                return -1;
            }
            if (write(tx,response,MANAGER_LIST_RESPONSE_BUFFER_SIZE)<0){
                fprintf(stderr,"write error: %s.\n", strerror(errno));
                close(tx);
                if (pthread_mutex_unlock(&box_table.mutex) != 0){
                    fprintf(stderr,"mutex_unlock error: could not leave the lock.\n");
                    return -1;
                }
                return -1;
            }
        }
    }
    
    if (pthread_mutex_unlock(&box_table.mutex) != 0){
        fprintf(stderr,"mutex_unlockk error: could not leave the lock.\n");
        return -1;
    }

    return 0;

}

int manager_list_subs(uint8_t* request){
    int tx = open((char*) &request[1], O_WRONLY);
    if (tx==-1){
        fprintf(stderr,"open error %s\n", strerror(errno));
        return -1;
    }
    uint8_t response[MANAGER_LIST_RESPONSE_BUFFER_SIZE];
    memset(response,0,MANAGER_LIST_RESPONSE_BUFFER_SIZE);
    uint8_t response_type = 12, last = 0;

    if (pthread_mutex_lock(&box_table.mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not get the lock.\n");
        return -1;
    }
    if (box_table.num_elem==0){
        response[0]=response_type;
        response[1]=1;

        if (write(tx,response,MANAGER_LIST_RESPONSE_BUFFER_SIZE)<0){
            fprintf(stderr,"write error: %s.\n", strerror(errno));
            close(tx);
            if (pthread_mutex_unlock(&box_table.mutex) != 0){
                fprintf(stderr,"mutex_unlock error: could not leave the lock.\n");
                return -1;
            }
            return -1;
        }
        if (pthread_mutex_unlock(&box_table.mutex) != 0){
            fprintf(stderr,"mutex_unlock error: could not leave the lock.\n");
            return -1;
        }
        return 0;
    }
}

int publisher_register(uint8_t* request){

    if (signal(SIGPIPE,sigpipe_handler)==SIG_ERR){
        exit(EXIT_FAILURE);
    }

    int rx = open((char*) &request[1], O_RDONLY);
    if (rx==-1){
        fprintf(stderr,"open error %s\n", strerror(errno));
        return -1;
    }

    box_t* box=box_register_pub(&box_table,(char*) &request[257]);
    if ( box == NULL){
        fprintf(stderr,"register_pub error: could not assign box to publisher.\n");
        close(rx);
        return -1;
    }

    char nome[BOX_LEN+1];
    nome[0] = '/';
    strcpy(nome+1,(char*) &request[257]);
    char buffer[PUBLISHER_BUFFER_SIZE];
    memset(buffer,0,PUBLISHER_BUFFER_SIZE);

    int fd = tfs_open(nome,TFS_O_APPEND);
    if (fd == -1){
        fprintf(stderr,"tfs_open error: could not open the box file.");
        if(tfs_close(fd)==-1) {
            fprintf(stderr,"tfs_close error: could not close box file.\n");
            return -1;
        }
        close(rx);
        return -1;
    }

    ssize_t bytes_read;
    while( (bytes_read=read(rx,buffer,sizeof(buffer))) != 0){
        if (buffer[0]!=9){
            fprintf(stderr,"publisher_register error: request format is incorrect.\n");
            if(tfs_close(fd)==-1) {
                fprintf(stderr,"tfs_close error: could not close box file.\n");
                return -1;
            }
            close(rx);
            return -1;
        }
        if(bytes_read==1)
            continue;
        if (bytes_read<0){
            fprintf(stderr,"read error: could not read publisher message.\n");
            if(tfs_close(fd)==-1) {
                fprintf(stderr,"tfs_close error: could not close box file.\n");
                return -1;
            }
            close(rx);
            return -1;
        }

        ssize_t wrote = tfs_write(fd,&buffer[1],MESSAGE_LEN);
        if (wrote<0){
            fprintf(stderr,"tfs_write error: there was an error writing to box.\n");
            close(rx);
            if(tfs_close(fd)==-1) {
                fprintf(stderr,"tfs_close error: could not close box file.\n");
                return -1;
            }
            return -1;
        }
        pthread_cond_broadcast(&box->wake_subs);
        if (pthread_mutex_lock(&box->mutex) != 0){
            fprintf(stderr,"mutex_lock error: could not get the lock.\n");
            exit(EXIT_FAILURE);
        }
        box->size += (uint64_t) wrote;
        if (pthread_mutex_unlock(&box->mutex) != 0){
            fprintf(stderr,"mutex_unlock error: could not leave the lock.\n");
            exit(EXIT_FAILURE);
        }
        memset(buffer+1,0,MESSAGE_LEN);


    }

    close(rx);

    if (box_unregister_pub(box)==-1){
        fprintf(stderr,"unregister_pub error: could not unregister the publisher from the box.\n");
        return -1;
    }
    
    if(tfs_close(fd)==-1) {
        fprintf(stderr,"tfs_close error: could not close box file.\n");
        return -1;
    }

    return 0;
}

int subscriber_register(uint8_t* request){

    int tx = open((char*) &request[1],O_WRONLY);
    if (tx==-1){
        fprintf(stderr,"open error: could not open fifo.\n");
        return -1;
    }

    box_t* box = box_register_sub(&box_table,(char*) &request[257]);
    if (box==NULL){
        fprintf(stderr,"register_sub error: couldn't assign box to sub.\n");
        close(tx);
        return -1;
    }

    char name[BOX_LEN+1];
    name[0]='/';
    memcpy(name+1,&request[257],BOX_LEN);

    int fd = tfs_open(name,0);
    if (fd==-1){
        fprintf(stderr,"tfs_open error: cant open box file.\n");
        if(tfs_close(fd)==-1) {
            fprintf(stderr,"tfs_close error: could not close box file.\n");
            return -1;
        }
        close(tx);
        return -1;
    }

    uint8_t response[SUBSCRIBER_BUFFER_SIZE];
    memset(response,0,SUBSCRIBER_BUFFER_SIZE);
    response[0]=10;
    

    while(1){
        if ( pthread_mutex_lock(&box->mutex) != 0 ){
            fprintf(stderr,"mutex_unlock error: could not get the lock.\n");
            return -1;
        }
        ssize_t readd=0;
        while ( (readd = tfs_read(fd,&response[1],MESSAGE_LEN))==0 ){
            pthread_cond_wait(&box->wake_subs,&box->mutex);
        }
        if (pthread_mutex_unlock(&box->mutex) != 0){
            fprintf(stderr,"mutex_unlock error: could not leave the lock.\n");
            return -1;
        }

        if (readd==-1){
            fprintf(stderr,"tfs_read error: could not read box file.\n");
            if(tfs_close(fd)==-1) {
                fprintf(stderr,"tfs_close error: could not close box file.\n");
                return -1;
            }
            close(tx);
            return -1;
        }

        if (write(tx,response,SUBSCRIBER_BUFFER_SIZE) <= 0){
            if (errno == EPIPE){
                if (box_unregister_sub(box)==-1){
                    fprintf(stderr,"unregister_pub error: could not unregister the publisher from the box.\n");
                    return -1;
                }
                return 0;
            }
            fprintf(stderr,"write error: could not write to fifo.\n");
            fflush(stderr);
            if(tfs_close(fd)==-1) {
                fprintf(stderr,"tfs_close error: could not close box file.\n");
                return -1;
            }
            close(tx);
            return -1;
        }

        memset(&response[1],0,MESSAGE_LEN);
    }
} 

void* handle_request(){
    uint8_t* buffer;
    //tenho de dar malloc?

    while(true){
        if ( (buffer = (uint8_t*) pcq_dequeue(&req_queue))==NULL ){
            return NULL;
        }
        fprintf(stderr,"[SERVER]- REQUEST TYPE: %d.\n", buffer[0]);
        switch((int) buffer[0]){
            case 1:
                if (publisher_register(buffer)==-1)
                    fprintf(stderr,"[INFO] FAILED HANDLING REQUEST.\n");
                else{
                    fprintf(stderr,"[INFO] SUCCESS HANDLING REQUEST.\n");
                }
                //create publisher thread and fifo;
                break;
            case 2:
                if (subscriber_register(buffer)==-1)
                    fprintf(stderr,"[INFO]-FAILED HANDLING REQUEST.\n");
                else{
                    fprintf(stderr,"[INFO]-SUCCESS HANDLING REQUEST.\n");
                }
                //create subscriber thread and fifo;
                break;
            case 3:
                //create box
                if (manager_create_box(buffer)==-1)
                    fprintf(stderr,"[INFO]-FAILED HANDLING REQUEST.\n");
                else
                    fprintf(stderr,"[SERVER]-SUCCESS HANDLING REQUEST.\n");
                break;
            case 5:
                //remove box
                if (manager_remove_box(buffer)==-1)
                    fprintf(stderr,"[INFO]-FAILED HANDLING REQUEST.\n");
                else
                    fprintf(stderr,"[SERVER]-SUCCESS HANDLING REQUEST.\n");
                break;
            case 7:
                //list all boxes
                if (manager_list_box(buffer)==-1)
                    fprintf(stderr,"[INFO]-FAILED HANDLING REQUEST.\n");
                else
                    fprintf(stderr,"[SERVER]-SUCCESS HANDLING REQUEST.\n");
                break;
            case 11:
                if (manager_list_subs(buffer)==-1)
                    fprintf(stderr,"[INFO]-FAILED HANDLING REQUEST.\n");
                else
                    fprintf(stderr,"[SERVER]-SUCCESS HANDLING REQUEST.\n");
                break;
            default:
                fprintf(stderr,"request error: unknown request.\n");
        }
    }
}

int main(int argc, char **argv) {

    int max_sessions=0;

    if (check_input_args(argc,EXPECTED_INPUT)==-1)
        exit(EXIT_FAILURE);

    for (int i=1; i<argc; i++){
        switch(i){
            case 1:
                strncpy(register_pipe_name,argv[i],PIPE_LEN-1);
                register_pipe_name[PIPE_LEN-1]=0;
                break;
            case 2:
                size_t len = strlen(argv[i]);
                for (int j=0;j<len;j++)
                    if (!isdigit(argv[i][j])){
                        fprintf(stderr,"mbroker error: max_sessions isn't a number.\n");
                        exit(EXIT_FAILURE);
                    }
                max_sessions = atoi(argv[i]);
                break;
            default:
                break;
        }
    }

    tfs_params params = {.max_inode_count=64, .max_block_count=1024, .max_open_files_count=16, .block_size=102400};

    if (tfs_init(&params)==-1){
        fprintf(stderr,"tfs_init error: couldn't start tfs.\n");
        exit(EXIT_FAILURE);
    }
    

    if (box_table_init(&box_table)==-1){
        tfs_destroy();
        exit(EXIT_FAILURE);
    }

    if (start_fifo(register_pipe_name)==-1){
        box_table_destroy(&box_table);
        tfs_destroy();
        exit(EXIT_FAILURE);
    }


    if (pcq_create(&req_queue,(size_t) max_sessions) == -1){
        box_table_destroy(&box_table);
        tfs_destroy();
        unlink(register_pipe_name);
        exit(EXIT_FAILURE);
    }

    const int size = max_sessions;
    pthread_t threads[size];
    for (int i=0;i<max_sessions;i++){
        if (pthread_create(&threads[i],NULL,handle_request,NULL) != 0){
            fprintf(stdout,"pthread_create error: could not create thread.\n");
            close_server();
        }
    }

    
    while(true){
        uint8_t* buffer = (uint8_t*) malloc(sizeof(uint8_t)*MAIN_BUFFER_SIZE);
        if (buffer == NULL){
            fprintf(stderr,"malloc error: no more memory accessible.\n");
            close_server();
        }
        memset(buffer,0,MAIN_BUFFER_SIZE);
        receiver_end = open(register_pipe_name, O_RDONLY);
        if (receiver_end == -1) {
            fprintf(stderr, "open error: %s\n", strerror(errno));
            close_server();
        }

        ssize_t readd = read(receiver_end,buffer,MAIN_BUFFER_SIZE);
        if (readd==-1){
            fprintf(stderr, "read error: %s\n", strerror(errno));
            close_server();
        }
        if (pcq_enqueue(&req_queue,buffer) == -1){
            close_server();
        }
    }

    close_server();

    return 0;
}
