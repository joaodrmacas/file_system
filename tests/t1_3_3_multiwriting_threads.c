#include "fs/operations.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

char file_contents1[] = "AAA!";
char target_path[] = "/f1";

typedef struct {
    char* name;
    int fd;
    bool last;
} read_write_t;

void* tfs_write_thread(void* arg){
    read_write_t* args = (read_write_t*) arg;
    if (args->last)
        assert(tfs_write(args->fd,"AAA!", 5) != -1);
    else 
        assert(tfs_write(args->fd,"AAA!", 4) != -1);
    return NULL;
}

void assert_contents_ok(char const *path) {
    int f = tfs_open(path, 0);
    assert(f != -1);

    char buffer[(sizeof(file_contents1)*3)-2];
    assert(tfs_read(f, buffer, sizeof(buffer)) == sizeof(buffer));
    printf("Wrote: %s\n", (char*) buffer );
    printf("Expected output: string with 12 characters with a ! every 3 char's\n");

    assert(tfs_close(f) != -1);
}

int main() {
    pthread_t p1,p2,p3;

    assert(tfs_init(NULL) != -1);

    // Write to hardlink and read original file
    int f = tfs_open(target_path, TFS_O_CREAT);
    assert(f != -1);

    read_write_t write_arg1 = {.name = target_path,.fd = f,.last=false};
    read_write_t write_arg2 = {.name = target_path,.fd = f,.last=false};
    read_write_t write_arg3 = {.name = target_path,.fd = f,.last=true};


    if (pthread_create(&p1,NULL,&tfs_write_thread,(void* ) &write_arg1) != 0){
        return -1;
    }

    if (pthread_create(&p2,NULL,&tfs_write_thread,(void*) &write_arg2) != 0){
        return -2;
    }

    if (pthread_create(&p3,NULL,&tfs_write_thread,(void*) &write_arg3) != 0){
        return -3;
    }

    pthread_join(p1,NULL);
    pthread_join(p2,NULL);
    pthread_join(p3,NULL);

    assert_contents_ok(target_path);

    assert(tfs_close(f) != -1);

    assert(tfs_destroy() != -1);

    printf("Successful test.\n");

    return 0;
}