#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define NUM_THREADS 3

typedef struct {
    char* file_path;
}open_arg_t;

void* create_file_thread_func(void* arg){
    open_arg_t *open_arg = (open_arg_t*) arg;
    int f = tfs_open(open_arg->file_path,TFS_O_CREAT);
    assert(f!=-1);
    assert (tfs_close(f) != -1);
    return NULL;
}

int main(){
    //int count = 0;
    pthread_t t1,t2,t3;
    char* file1_path = "/f1";
    char* file2_path = "/f2";
    char* file3_path = "/f3";
    open_arg_t open_arg1 = { .file_path = file1_path};
    open_arg_t open_arg2 = { .file_path = file2_path};
    open_arg_t open_arg3 = { .file_path = file3_path};

    assert(tfs_init(NULL) != -1);

    //create files with different names using threads;

    if (pthread_create(&t1,NULL,create_file_thread_func,&open_arg1) != 0){
        printf("pthread_create error: couldn't create a new thread.\n");
        return -1;
    }
    if (pthread_create(&t2,NULL,create_file_thread_func,&open_arg2) != 0){
        printf("pthread_create error: couldn't create a new thread.\n");
        return -1;
    }
    if (pthread_create(&t3,NULL,create_file_thread_func,&open_arg3) != 0){
        printf("pthread_create error: couldn't create a new thread.\n");
        return -1;
    }

    pthread_join(t1,NULL);
    pthread_join(t2,NULL);
    pthread_join(t3,NULL);

    assert(tfs_destroy() != -1);
    printf("Successful test.\n");

}