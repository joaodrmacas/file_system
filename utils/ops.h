#ifndef __UTILS_OPS_H__
#define __UTILS_OPS_H__

#include "operations.h"

#define PIPE_LEN (256)
#define BOX_LEN (32)
#define MESSAGE_LEN (1024)

#define MAIN_BUFFER_SIZE (1029)
#define REGISTRY_BUFFER_SIZE (289)
#define MANAGER_CREATE_BUFFER_SIZE (289)
#define MANAGER_REMOVE_BUFFER_SIZE (289)
#define MANAGER_LIST_REQUEST_BUFFER_SIZE (257)
#define MANAGER_RESPONSE_BUFFER_SIZE (1029)
#define MANAGER_LIST_RESPONSE_BUFFER_SIZE (58)
#define PUBLISHER_BUFFER_SIZE (1025)
#define SUBSCRIBER_BUFFER_SIZE (1025)


int check_input_args(int argc,int expectedArgs);
int start_fifo(char* fifo);
int create_box(char* box_name);

#endif