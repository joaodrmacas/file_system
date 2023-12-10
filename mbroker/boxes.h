#ifndef __MBROKER_BOXES_H__
#define __MBROKER_BOXES_H__

#include "ops.h"
#include <pthread.h>
#include <stdint.h>

#define TABLE_STARTING_CAPACITY (8)

typedef struct{
    char file_name[BOX_LEN];
    uint64_t pub_num;
    uint64_t sub_num;
    uint64_t size;
    pthread_mutex_t mutex;
    pthread_cond_t wake_subs;
}box_t;

typedef struct{
    char** subs;
    box_t** boxes;
    size_t capacity,size,num_elem;
    pthread_mutex_t mutex;
}box_table_t;



int box_add(char* filename,box_table_t* box_table);
int box_remove(char* filename,box_table_t* box_table);

int box_table_init(box_table_t* box_table);
void box_table_destroy(box_table_t* box_table);
int expand_table(box_table_t* box_table);
box_t* box_find(box_table_t* box_table,char* filename);

box_t* box_register_pub(box_table_t* box_table,char* filename);
box_t* box_register_sub(box_table_t* box_table, char* filename);
int box_unregister_pub(box_t* box);
int box_unregister_sub(box_t* box);


#endif