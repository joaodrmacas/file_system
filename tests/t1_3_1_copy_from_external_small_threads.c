#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define NUM_THREADS 3

typedef struct {
    char *src;
    char *dst;
} copy_arg_t;

void *copy_thread_func(void *arg) {
    copy_arg_t *copy_arg = (copy_arg_t*)arg;
    tfs_copy_from_external_fs(copy_arg->src, copy_arg->dst);
    return NULL;
}

int main() {

    char *str_ext_file = "BBB!";
    char *path_copied_file = "/f1";
    char *path_src = "tests/file_to_copy.txt";
    char buffer[40];

    assert(tfs_init(NULL) != -1);

    int f;
    ssize_t r;

    pthread_t copy_threads[NUM_THREADS];
    copy_arg_t copy_arg = {
        .src = path_src,
        .dst = path_copied_file,
    };
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&copy_threads[i], NULL, copy_thread_func, &copy_arg);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(copy_threads[i], NULL);
    }

    f = tfs_open(path_copied_file, TFS_O_CREAT);
    assert(f != -1);

    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    assert(r == strlen(str_ext_file));
    assert(!memcmp(buffer, str_ext_file, strlen(str_ext_file)));

    // Repeat the copy to the same file
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&copy_threads[i], NULL, copy_thread_func, &copy_arg);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(copy_threads[i], NULL);
    }

    f = tfs_open(path_copied_file, TFS_O_CREAT);
    assert(f != -1);

    // Contents should be overwriten, not appended
    r = tfs_read(f, buffer, sizeof(buffer) - 1);
    assert(r == strlen(str_ext_file));
    assert(!memcmp(buffer, str_ext_file, strlen(str_ext_file)));

    printf("Successful test.\n");

    return 0;
}