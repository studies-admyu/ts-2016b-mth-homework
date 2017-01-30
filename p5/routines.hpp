#pragma once

#include <cstdlib>

#define EPOLL_WAIT_TIMEOUT_MSEC 10000
#define STORAGE_ELEMENTS_MAX 1024
#define EVENTS_MAX 256

struct KeyValuePair
{
    int key;
    int value;
};

const size_t STORAGE_SIZE = sizeof(size_t) + STORAGE_ELEMENTS_MAX * sizeof(KeyValuePair);

int master_loop(int port);

int worker_loop(int client_socket, int stg_sem_id, int stg_shm_id);
