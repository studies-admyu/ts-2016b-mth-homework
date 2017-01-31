#pragma once

#include <cstdlib>

#include <unistd.h>
#include <semaphore.h>

#define EPOLL_WAIT_TIMEOUT_MSEC 10000
#define STORAGE_ELEMENTS_MAX 1024
#define EVENTS_MAX 256

struct KeyValuePair
{
    int key;
    int value;
    int ttl;
    time_t created;
};

struct WorkerInfo
{
    pid_t pid;
    int pass_fd;
    bool free;
};

const size_t STORAGE_SIZE = sizeof(size_t) + STORAGE_ELEMENTS_MAX * sizeof(KeyValuePair);

int pass_fd_write(int pass_fd, int fd);
int pass_fd_read(int pass_fd, int* fd);

int master_loop(int port);

int worker_loop(int pass_fd, sem_t* stg_sem_id, int stg_shm_id);
