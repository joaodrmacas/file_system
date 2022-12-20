#include "../fs/operations.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    const char *file_path = "/f1";
    const char *link_path = "/l1";

    assert(tfs_init(NULL) != -1);

    int f = tfs_open(file_path,TFS_O_CREAT);
    assert(f != -1);

    assert(tfs_close(f)!= -1);

    assert(tfs_link(file_path, link_path) != -1);
    // Unlink file
    assert(tfs_unlink(link_path) != -1);

    assert(tfs_unlink(link_path) == -1);

    printf("Successful test.\n");
}
