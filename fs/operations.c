#include "operations.h"
#include "config.h"
#include "state.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "betterassert.h"
#include <pthread.h>

#define BUFFER_SIZE 128
#define BLOCK_SIZE tfs_default_params().block_size

/* TODO LIST

-> Alterar o copy from external
-> alterar o unlink para dar a ficheiros abertos e fazer um teste sobre isto.
-> fazer um teste sobre o copy_from_external quando tenta copiar um ficheiro maior que o bloco.
-> meter os testes a dar return values.
-> Fazer locks para a open file table.
*/

/* 

    Multithreading tests

    // -> Quando tamos a dar unlink garantir que estamos a decrementar a link count no numero total;
    // -> Quando formos criar um hardlink garantir que incrementamos duas vezes;
    // -> Se tivermos a criar 2 ficheiros com o mm nome, dar lock enquanto adicioanamos um para ir procurar o outro;
    // -> Escrever no mesmo ficheiro ao mesmo tempo;


    // -> Criar 3 ficheiros ao mm tempo com nomes diferentes e com nomes iguais


// Duvidas:
-> os mutex locks meto dentro ou fora dos for loops? 
-> linha 291, o mutex lock tem de ser antes do if?
-> posso tirar o const do inode no tfs_read()?
-> linha 214 do state.c
-> precisamos de lock no inode create? 
-> precisamos de lock no inode get?


// Fazer close quando dá erro? 

*/


tfs_params tfs_default_params() {
    tfs_params params = {
        .max_inode_count = 64,
        .max_block_count = 1024,
        .max_open_files_count = 16,
        .block_size = 1024,
    };
    return params;
}

int tfs_init(tfs_params const *params_ptr) { 
    tfs_params params;
    if (params_ptr != NULL) {
        params = *params_ptr;
    } else {
        params = tfs_default_params();
    }

    if (state_init(params) != 0) {
        return -1;
    }

    // create root inode
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    if (state_destroy() != 0) {
        return -1;
    }
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}

/**
 * Looks for a file.
 *
 * Note: as a simplification, only a plain directory space (root directory only)
 * is supported.
 *
 * Input:
 *   - name: absolute path name
 *   - root_inode: the root directory inode
 * Returns the inumber of the file, -1 if unsuccessful.
 */
static int tfs_lookup(char const *name, inode_t const *root_inode) {
    // TODO: assert that root_inode is the root directory
    if (!valid_pathname(name)) {
        return -1;
    }

    if (root_inode->i_node_type != T_DIRECTORY){
        fprintf(stderr,"tfs_lookup error: root inode isn't a directory\n");
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(root_inode, name);
}

int tfs_open(char const *name, tfs_file_mode_t mode) {
    // Checks if the path name is valid
    if (!valid_pathname(name)) {
        return -1;
    }

    inode_t *root_dir_inode = inode_get(ROOT_DIR_INUM);
    ALWAYS_ASSERT(root_dir_inode != NULL,
                  "tfs_open error: root dir inode must exist");
    int inum = tfs_lookup(name, root_dir_inode);

    size_t offset;

    if (inum >= 0) {
        // The file already exists
        inode_t *inode = inode_get(inum);

        if (inode->i_node_type == T_SYMLINK){
            return tfs_open(inode->i_symlink_name,mode);
        }

        pthread_rwlock_wrlock(&inode_rwl_table[inum]);

        ALWAYS_ASSERT(inode != NULL,
                      "tfs_open error: directory files must have an inode");

        // Truncate (if requested)
        if (mode & TFS_O_TRUNC) {
            if (inode->i_data.i_size > 0) {
                data_block_free(inode->i_data.i_data_block);
                inode->i_data.i_size = 0;
            }        }
        // Determine initial offset
        if (mode & TFS_O_APPEND) {
            offset = inode->i_data.i_size;
        } else {
            offset = 0;
        }
        pthread_rwlock_unlock(&inode_rwl_table[inum]);
    } 
    else if (mode & TFS_O_CREAT) {
        // The file does not exist; the mode specified that it should be created
        // Create inode


        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1; // no space in inode table
        }

        // Add entry in the root directory
        if (add_dir_entry(root_dir_inode, name + 1, inum) == -1) {
            inode_delete(inum);
            return -1; // no space in directory
        }

        offset = 0;
    } else {
        return -1;
    }

    // Finally, add entry to the open file table and return the corresponding
    // handle
    return add_to_open_file_table(inum, offset);

    // Note: for simplification, if file was created with TFS_O_CREAT and there
    // is an error adding an entry to the open file table, the file is not
    // opened but it remains created
}

int tfs_close(int fhandle) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1; // invalid fd
    }
    remove_from_open_file_table(fhandle);
    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    pthread_mutex_lock(&file->mutex);

    //  From the open file table entry, we get the inode
    inode_t *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_write error: inode of open file deleted");

    // Determine how many bytes to write
    size_t block_size = state_block_size();

    if (to_write + file->of_offset > block_size) {
        to_write = block_size - file->of_offset;
    }

    if (to_write > 0) {
        pthread_rwlock_wrlock(&inode_rwl_table[file->of_inumber]);
        if (inode->i_data.i_size == 0) {
            // If empty file, allocate new block
            int bnum = data_block_alloc();
            if (bnum == -1) {
                pthread_rwlock_unlock(&inode_rwl_table[file->of_inumber]);
                pthread_mutex_unlock(&file->mutex);
                return -1; // no space
            }

            inode->i_data.i_data_block = bnum;
        }
        

        void *block = data_block_get(inode->i_data.i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_write: data block deleted mid-write");

        // Perform the actual write
        

        memcpy(block + file->of_offset, buffer, to_write);
        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_write;
        if (file->of_offset > inode->i_data.i_size) {
            inode->i_data.i_size = file->of_offset;
        }
        pthread_rwlock_unlock(&inode_rwl_table[file->of_inumber]);
    }
    pthread_mutex_unlock(&file->mutex);
    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    pthread_mutex_lock(&file->mutex);

    // From the open file table entry, we get the inode
    inode_t *inode = inode_get(file->of_inumber);
    ALWAYS_ASSERT(inode != NULL, "tfs_read error: inode of open file deleted");

    pthread_rwlock_rdlock(&inode_rwl_table[file->of_inumber]);
    // Determine how many bytes to read
    size_t to_read = inode->i_data.i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }
        if (to_read > 0) {
        void *block = data_block_get(inode->i_data.i_data_block);
        ALWAYS_ASSERT(block != NULL, "tfs_read error: data block deleted mid-read");

        // Perform the actual read
        memcpy(buffer, block + file->of_offset, to_read);
        // The offset associated with the file handle is incremented accordingly
        file->of_offset += to_read;
    }
    pthread_rwlock_unlock(&inode_rwl_table[file->of_inumber]);
    pthread_mutex_unlock(&file->mutex);
    return (ssize_t)to_read;
}

int tfs_unlink(char const *target) {
    inode_t *rootInode=inode_get(ROOT_DIR_INUM), *targetInode=NULL;
    int targetINumber = 0;

    targetINumber = find_in_dir(rootInode,++target);

    if (targetINumber == -1){
        fprintf(stderr, "tfs_unlink error: /%s doesn't exist.\n", target);
        return -1;
    }

    targetInode = inode_get(targetINumber);

    pthread_rwlock_wrlock(&inode_rwl_table[targetINumber]);

    switch(targetInode->i_node_type){
        case T_DIRECTORY: {
            fprintf(stderr, "tfs_unlink error: you can't delete directory.\n");
            pthread_rwlock_unlock(&inode_rwl_table[targetINumber]);
            return -1;
        } break;
        case T_FILE:
            if( targetInode->i_data.i_links_count == 1){
                if(get_open_file_entry_from_inumber(targetINumber)){
                    fprintf(stderr, "tfs_unlink error: you can't delete a file that's open.\n");
                    return -1;
                }
                if(targetInode->i_data.i_size>0 ){
                    data_block_free(targetInode->i_data.i_data_block);
                }
                inode_delete(targetINumber);
            } else{
                targetInode->i_data.i_links_count--;
            }
            if (clear_dir_entry(rootInode, target) == -1){
                fprintf(stderr, "tfs_unlink error: /%s does not exit.\n", target);
                pthread_rwlock_unlock(&inode_rwl_table[targetINumber]);
                return -1;
            }
            break;
        case T_SYMLINK:
            if (clear_dir_entry(rootInode, target) == -1){
                    fprintf(stderr, "tfs_unlink error: /%s does not exit.\n", target);
                    pthread_rwlock_unlock(&inode_rwl_table[targetINumber]);
                    return -1;
            }
            break;
        default:
            PANIC("tfs_unlink error: unknown file type");
    }

    pthread_rwlock_unlock(&inode_rwl_table[targetINumber]);

    return 0;
}


int tfs_sym_link(char const *target, char const *link_name) {
    int targetINumber=0, symlinkINumber=0;
    inode_t *symlinkINode=NULL, *rootInode=NULL;

    rootInode = inode_get(ROOT_DIR_INUM);
    targetINumber = tfs_lookup(target,rootInode);

    if (targetINumber == -1){
        fprintf(stderr, "tfs_sym_link error: %s does not exit.\n", target);
        return -1;
    }

    symlinkINumber = inode_create(T_SYMLINK);

    if (symlinkINumber == -1){
        fprintf(stderr,"tfs_sym_link error: couldn't create the inode.\n");
        return -1;
    }

    if (add_dir_entry(rootInode,++link_name,symlinkINumber) == -1){
        fprintf(stderr,"tfs_sym_link error: Couldn't add a new entry to root.\n");
        return -1;
    }

    symlinkINode = inode_get(symlinkINumber);

    pthread_rwlock_wrlock(&inode_rwl_table[symlinkINumber]);
    strcpy(symlinkINode->i_symlink_name, target);
    pthread_rwlock_unlock(&inode_rwl_table[symlinkINumber]);

    return 0;
}


//Duvidas:
//Temos que verificar sempre se existe um ficheiro target e se já existe um ficheiro com o nome que tamos a tentar criar? 
int tfs_link(char const *target, char const *link_name) {
    int targetINumber = 0;
    inode_t *targetInode=NULL, *rootInode=NULL;

    //usar o add_to_dir para adicionar um nome (nome do link) à root e criar um contador de apontadores no inode

    rootInode = inode_get(ROOT_DIR_INUM);
    targetINumber = tfs_lookup(target,rootInode);
    
    if (targetINumber == -1){
        fprintf(stderr, "tfs_link error: %s does not exist.\n", target);
        return -1;
    }

    targetInode = inode_get(targetINumber);

    if (targetInode->i_node_type == T_SYMLINK){
        fprintf(stderr, "tfs_link error: can't create a hardlink from softlink.\n");
        return -1;
    }
    
    if (add_dir_entry(rootInode,++link_name,targetINumber) == -1){
        fprintf(stderr,"tfs_link error: there was an error adding entry to root.\n");
        return -1;
    }

    pthread_rwlock_wrlock(&inode_rwl_table[targetINumber]);
    targetInode->i_data.i_links_count++;    
    pthread_rwlock_unlock(&inode_rwl_table[targetINumber]);

    return 0;
}
 
// Duvida: Sempre que damos um open temos que verificar se já estava aberto? 
int tfs_copy_from_external_fs(char const *source_path, char const *dest_path) {
    int fd = 0, copyHandler=0;
    char buffer[BUFFER_SIZE];
    memset(buffer,0,sizeof(buffer));
    ssize_t bytesRead;

    if ((fd = open(source_path,O_RDONLY)) < 0){
        fprintf(stderr, "open error: %s.\n", strerror(errno));
        return -1;
    }

    if (lseek(fd,0,SEEK_END) > BLOCK_SIZE){
        fprintf(stderr,"tfs_copy_from_external_fs error: %s is too big.\n", source_path);
        return -1;
    }

    lseek(fd,0,SEEK_SET);

    if((copyHandler = tfs_open(dest_path, TFS_O_TRUNC | TFS_O_CREAT ))<0){
        fprintf(stderr, "tfs_copy_from_external_fs error: couldn't open %s.\n",dest_path);
        return -1;
    }

    while ( (bytesRead = read(fd ,buffer, sizeof(buffer)-1)) != 0){

        if (bytesRead < 0){
            fprintf(stderr, "read error: %s.\n", strerror(errno));
            return -1;
        }

        if (tfs_write(copyHandler, buffer, strlen(buffer)) < 0){
            fprintf(stderr, "tfs_copy_from_external_fs error: couldn't write to %s.\n",dest_path);
            return -1;
        }
        memset(buffer,0,sizeof(buffer));
    }

    if ( (fd = close(fd)) < 0){
        fprintf(stderr, "close error: %s.\n", strerror(errno));
        return -1;
    } 

    if (tfs_close(copyHandler)==-1){
        fprintf(stderr, "tfs_copy_from_external_fs error: couldn't close %s.\n", dest_path);
        return -1;
    }

    return 0;
}

