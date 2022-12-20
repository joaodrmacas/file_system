#include "fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {

    char *path_copied_file = "/f1";
    char *path_src = "tests/file_to_copy_over1028.txt";

    assert(tfs_init(NULL) != -1);

    assert(tfs_copy_from_external_fs(path_src, path_copied_file)==-1);

    printf("Successful test.\n");

    return 0;
}
