#include "boxes.h"
#include "operations.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int box_table_init(box_table_t* box_table){
    box_table->boxes = (box_t**) malloc(sizeof(box_t*)*TABLE_STARTING_CAPACITY);
    box_table->subs = (char**) malloc(sizeof(char*)*TABLE_STARTING_CAPACITY);
    if (box_table->boxes==NULL){
        fprintf(stderr,"malloc error: couldn't create box table\n.");
        return -1;
    }
    for (int i=0;i<TABLE_STARTING_CAPACITY;i++){
        box_table->boxes[i] = NULL;
        box_table->subs[i]=NULL;
    }
    pthread_mutex_init(&box_table->mutex,NULL);
    box_table->capacity = TABLE_STARTING_CAPACITY;
    box_table->size=0;
    box_table->num_elem = 0;
    return 0;
}

void box_table_destroy(box_table_t* box_table){
    for (int i=0;i<box_table->size;i++){
        if (box_table->boxes[i]!=NULL){
            pthread_mutex_destroy(&box_table->boxes[i]->mutex);
            pthread_cond_destroy(&box_table->boxes[i]->wake_subs);
            free(box_table->boxes[i]);
        }
        if (box_table->subs!=NULL)
            free(box_table->subs[i]);
    }
    pthread_mutex_destroy(&box_table->mutex);
    free(box_table->boxes);
    free(box_table->subs);
}

int expand_table(box_table_t* box_table){
    box_table->capacity*=2;
    box_table->boxes = (box_t**) realloc(box_table->boxes,sizeof(box_t*)*box_table->capacity);
    if (box_table->boxes==NULL){
        fprintf(stderr,"realloc error: couldn't resize box table\n.");
        return -1;
    }
     for (int i= (int) (box_table->capacity/2);i<box_table->capacity;i++){
        box_table->boxes[i] = NULL;
    }
    return 0;
}

int expand_subs(box_table_t* box_table){
    box_table->capacity*=2;
    box_table->boxes = (box_t**) realloc(box_table->boxes,sizeof(box_t*)*box_table->capacity);
    if (box_table->boxes==NULL){
        fprintf(stderr,"realloc error: couldn't resize box table\n.");
        return -1;
    }
     for (int i= (int) (box_table->capacity/2);i<box_table->capacity;i++){
        box_table->boxes[i] = NULL;
    }
    return 0;
}

int box_add(char* filename,box_table_t* box_table){
    box_t* box = (box_t*) malloc(sizeof(box_t));
    memset(box,0,BOX_LEN);
    strcpy(box->file_name,filename);
    box->pub_num=0;
    box->sub_num=0;
    box->size=0;
    pthread_mutex_init(&box->mutex,NULL);
    pthread_cond_init(&box->wake_subs,NULL);

    if (pthread_mutex_lock(&box_table->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not get the lock.\n");
        return -1;
    }
    if (box_table->size==box_table->capacity){
        if (expand_table(box_table)==-1){
            if (pthread_mutex_unlock(&box_table->mutex) != 0){
                fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
                return -1;
            }
            return -1;
        }
    }

    if (box_table->size==box_table->num_elem){
        box_table->boxes[box_table->size]=box;
        box_table->size++;
        box_table->num_elem++;
        if (pthread_mutex_unlock(&box_table->mutex) != 0){
            fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
            return -1;
        }
        return 0;
    }

    for (int i=0;i<box_table->size;i++){
        if (box_table->boxes[i]==NULL){
            box_table->boxes[i]=box;
            if (box_table->num_elem==box_table->size)
                box_table->size++;
            box_table->num_elem++;
            if (pthread_mutex_unlock(&box_table->mutex) != 0){
                fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
                return -1;
            }
            return 0;
        }
    }

    if (pthread_mutex_unlock(&box_table->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
        return -1;
    }
    fprintf(stderr,"box_add panic: unexpected behaviour.\n");
    return -1;
}

int box_remove(char* filename,box_table_t* box_table){
    if (pthread_mutex_lock(&box_table->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not get the lock.\n");
        return -1;
    }
    for (int i=0;i<box_table->size;i++){
        if (box_table->boxes[i]!=NULL){
            if ( strcmp(box_table->boxes[i]->file_name,filename)==0 ){
                pthread_mutex_destroy(&box_table->boxes[i]->mutex);
                pthread_cond_destroy(&box_table->boxes[i]->wake_subs);
                free(box_table->boxes[i]);
                box_table->boxes[i]=NULL;
                if (box_table->size==i+1)
                    box_table->size--;
                box_table->num_elem--;
                if (pthread_mutex_unlock(&box_table->mutex) != 0){
                    fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
                    return -1;
                }
                return 0;
            } 
        }
    }
    if (pthread_mutex_unlock(&box_table->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
        return -1;
    }
    return -1;
}

box_t* box_find(box_table_t* box_table,char* filename){
    if (pthread_mutex_lock(&box_table->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not get the lock.\n");
        exit(EXIT_FAILURE);
    }
    for (int i=0;i<box_table->size;i++){
        if (box_table->boxes[i]!=NULL){
            if (strcmp(box_table->boxes[i]->file_name,filename)==0){
                if (pthread_mutex_unlock(&box_table->mutex) != 0){
                    fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
                    exit(EXIT_FAILURE);
                }
                return box_table->boxes[i];
            }
        }
    }
    if (pthread_mutex_unlock(&box_table->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
        exit(EXIT_FAILURE);
    }
    return NULL;
}

box_t* box_register_pub(box_table_t* box_table,char* filename){
    box_t* box = box_find(box_table,filename);
    if (box==NULL)
        return NULL;

    if (pthread_mutex_lock(&box->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not get the lock.\n");
        exit(EXIT_FAILURE);
    }
    if (box->pub_num==1){
        if (pthread_mutex_unlock(&box->mutex) != 0){
            fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
            exit(EXIT_FAILURE);
        }
        return NULL;
    }
    
    box->pub_num++;

    if (pthread_mutex_unlock(&box->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
        exit(EXIT_FAILURE);
    }
    return box;
}

box_t* box_register_sub(box_table_t* box_table, char* filename){
    box_t* box = box_find(box_table,filename);
    if (box==NULL){
        return NULL;
    }

    if (pthread_mutex_lock(&box->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not get the lock.\n");
        exit(EXIT_FAILURE);
    }

    box->sub_num++;

    if (pthread_mutex_unlock(&box->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
        exit(EXIT_FAILURE);
    }
    return box ;
}

int box_unregister_pub(box_t* box){
    if (box==NULL)
        return -1;

    if (pthread_mutex_lock(&box->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not get the lock.\n");
        exit(EXIT_FAILURE);
    }

    if (box->pub_num==0){
        if (pthread_mutex_unlock(&box->mutex) != 0){
            fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
            exit(EXIT_FAILURE);
        }
        return -1;
    }

    box->pub_num--;

    if (pthread_mutex_unlock(&box->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}

int box_unregister_sub(box_t* box){
    if (box==NULL)
        return -1;

    if (pthread_mutex_lock(&box->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not get the lock.\n");
        exit(EXIT_FAILURE);
    }

    if (box->sub_num==0){
        if (pthread_mutex_unlock(&box->mutex) != 0){
            fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
            exit(EXIT_FAILURE);
        }
        return -1;
    }

    box->sub_num--;
    if (pthread_mutex_unlock(&box->mutex) != 0){
        fprintf(stderr,"mutex_lock error: could not leave the lock.\n");
        exit(EXIT_FAILURE);
    }
    return 0;
}

