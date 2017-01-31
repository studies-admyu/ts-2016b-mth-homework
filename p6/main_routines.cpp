#include <ctime>
#include <iostream>
#include <list>
#include <random>

#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <wait.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/mman.h>

#include <netinet/in.h>

#include "routines.hpp"

#define WORKERS_COUNT 4

const char* posix_shm_name = "/keyvalue_storage_shm";
const char* posix_sem_name = "/keyvalue_storage_sem";

std::random_device rd;

int pass_fd_write(int pass_fd, int fd)
{
    char buffer = '\0';
    struct iovec iov;

    iov.iov_base = &buffer;
    iov.iov_len = sizeof(char);

    msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;


    union {
        struct cmsghdr cmsghdr;
        char control[CMSG_SPACE(sizeof(int))];
    } cmsgu;

    msg.msg_control = cmsgu.control;
    msg.msg_controllen = sizeof(cmsgu.control);

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;

    *((int*)CMSG_DATA(cmsg)) = fd;

    return (sendmsg(pass_fd, &msg, 0) > 0)? 0: -1;
}

int pass_fd_read(int pass_fd, int* fd){
    char buffer = '\0';
    struct iovec iov;

    iov.iov_base = &buffer;
    iov.iov_len = sizeof(char);

    msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;


    union {
        struct cmsghdr cmsghdr;
        char control[CMSG_SPACE(sizeof(int))];
    } cmsgu;

    msg.msg_control = cmsgu.control;
    msg.msg_controllen = sizeof(cmsgu.control);

    if (recvmsg(pass_fd, &msg, 0) <= 0) {
        return -1;
    }

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg) {
        *fd = -1;
        return 0;
    } else if (cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
        *fd = -1;
        return 0;
    } else if (cmsg->cmsg_level != SOL_SOCKET) {
        return -1;
    } else if (cmsg->cmsg_type != SCM_RIGHTS) {
        return -1;
    } else {
        *fd = *((int*)CMSG_DATA(cmsg));
        return 0;
    }
}

int setup_signals_main(int* signal_fd)
{
    sigset_t signal_mask;

    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGINT);
    sigaddset(&signal_mask, SIGQUIT);

    if (sigprocmask(SIG_BLOCK, &signal_mask, NULL) == -1) {
        return -1;
    }

    *signal_fd = signalfd(-1, &signal_mask, 0);
    if (*signal_fd == -1) {
        return -1;
    }
    return 0;
}

int start_listening(int epoll_fd, int server_socket, int signal_fd, int port)
{
    epoll_event events[2];
    events[0].data.fd = server_socket;
    events[0].events = EPOLLIN | EPOLLET;

    events[1].data.fd = signal_fd;
    events[1].events = EPOLLIN | EPOLLET;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_socket, &(events[0])) != 0) {
        return -1;
    }

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, signal_fd, &(events[1])) != 0) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_socket, &(events[0]));
        return -1;
    }

    sockaddr_in socket_address;
    socket_address.sin_addr.s_addr = htonl(INADDR_ANY);
    socket_address.sin_port = htons(port);
    socket_address.sin_family = AF_INET;

    if (bind(server_socket, (sockaddr*)&socket_address, sizeof(socket_address)) == -1) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_socket, &(events[0]));
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, signal_fd, &(events[1]));
        return -1;
    }

    if (listen(server_socket, WORKERS_COUNT) == -1) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_socket, &(events[0]));
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, signal_fd, &(events[1]));
        return -1;
    }

    return 0;
}

void clear_epoll_main(int epoll_fd, int server_socket, int signal_fd)
{
    epoll_event events[2];
    events[0].data.fd = server_socket;
    events[0].events = EPOLLIN | EPOLLET;

    events[1].data.fd = signal_fd;
    events[1].events = EPOLLIN | EPOLLET;

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_socket, &(events[0]));
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, signal_fd, &(events[1]));
}

int add_worker_pass_fd(int epoll_fd, int worker_pass_fd)
{
    epoll_event event;
    event.data.fd = worker_pass_fd;
    event.events = EPOLLIN | EPOLLET;

    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, worker_pass_fd, &event);
}

int remove_worker_pass_fd(int epoll_fd, int worker_pass_fd)
{
    epoll_event event;
    event.data.fd = worker_pass_fd;
    event.events = EPOLLIN | EPOLLET;

    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, worker_pass_fd, &event);
}

int init_storage_main(sem_t** stg_sem_id, int* stg_shm_id, void** stg_shm_ptr)
{
    *stg_shm_id = shm_open(posix_shm_name, O_CREAT | O_RDWR, 0666);
    if (*stg_shm_id == -1) {
        return -1;
    }
    if (ftruncate(*stg_shm_id, STORAGE_SIZE) == -1) {
        shm_unlink(posix_shm_name);
        close(*stg_shm_id);
        return -1;
    }
    *stg_sem_id = sem_open(posix_sem_name, O_CREAT | O_RDWR, 0666, 1);
    if (*stg_sem_id == SEM_FAILED) {
        shm_unlink(posix_shm_name);
        close(*stg_shm_id);
        return -1;
    }

    *stg_shm_ptr = mmap(NULL, STORAGE_SIZE, PROT_WRITE, MAP_SHARED, *stg_shm_id, 0);
    if (!*stg_shm_ptr) {
        shm_unlink(posix_shm_name);
        close(*stg_shm_id);
        sem_unlink(posix_sem_name);
        sem_close(*stg_sem_id);
        return -1;
    }
    size_t* entries_count = ((size_t*)*stg_shm_ptr);
    *entries_count = 0;
    return 0;
}

void destroy_storage_main(sem_t* stg_sem_id, int stg_shm_id, void* stg_shm_ptr) {
    munmap(stg_shm_ptr, STORAGE_SIZE);

    shm_unlink(posix_shm_name);
    sem_unlink(posix_sem_name);

    close(stg_shm_id);
    sem_close(stg_sem_id);
}

void create_workers(std::list<WorkerInfo>& workers_list, int epoll_fd, sem_t* stg_sem_id, int stg_shm_id)
{
    for (int i = 0; i < WORKERS_COUNT; ++i) {
        pid_t pid;
        int pass_fd[2];
        socketpair(AF_LOCAL, SOCK_STREAM, 0, pass_fd);
        add_worker_pass_fd(epoll_fd, pass_fd[1]);
        if ((pid = fork()) == 0) {
            close(pass_fd[1]);
            exit(worker_loop(pass_fd[0], stg_sem_id, stg_shm_id));
        } else {
            close(pass_fd[0]);
            WorkerInfo worker_info;
            worker_info.pid = pid;
            worker_info.pass_fd = pass_fd[1];
            worker_info.free = true;
            workers_list.push_back(worker_info);
        }
    }
}

void wait_for_workers(std::list<WorkerInfo>& workers_list, int epoll_fd)
{
    while (workers_list.size() > 0) {
        pid_t zombie_pid = waitpid(-1, NULL, 0);

        for (auto worker_i = workers_list.begin(); worker_i != workers_list.end(); ++worker_i) {
            if (worker_i->pid == zombie_pid) {
                remove_worker_pass_fd(epoll_fd, worker_i->pass_fd);
                close(worker_i->pass_fd);
                workers_list.erase(worker_i, std::next(worker_i));
                break;
            }
        }
    }
}

void client_accept(std::list<WorkerInfo>& workers_list, int server_socket)
{
    std::list<WorkerInfo> free_workers;
    for (auto worker_i = workers_list.cbegin(); worker_i != workers_list.cend(); ++worker_i) {
        if (worker_i->free) {
            free_workers.push_back(*worker_i);
        }
    }

    int client_socket = accept(server_socket, NULL, 0);
    if (client_socket == -1) {
        return;
    }
    if (free_workers.size() == 0) {
        shutdown(client_socket, SHUT_RDWR);
        close(client_socket);
        return;
    }

    std::uniform_int_distribution<int> rand_dist(0, free_workers.size() - 1);
    auto free_worker_i = free_workers.cbegin();

    std::advance(free_worker_i, rand_dist(rd));

    if (pass_fd_write(free_worker_i->pass_fd, client_socket) == -1) {
        shutdown(client_socket, SHUT_RDWR);
        close(client_socket);
    } else {
        for (auto worker_i = workers_list.begin(); worker_i != workers_list.end(); ++worker_i) {
            if (worker_i->pid == free_worker_i->pid) {
                worker_i->free = false;
                break;
            }
        }
    }
}

void clean_old_entries(sem_t* stg_sem_id, void* stg_shm_ptr)
{
    if (sem_wait(stg_sem_id) == -1) {
        throw std::runtime_error("Unable to lock the semaphore");
    }

    size_t* elements_count= (size_t*)stg_shm_ptr;
    KeyValuePair* kv_elements = (KeyValuePair*)(elements_count + 1);

    if (*elements_count > 0) {
        time_t current_time = time(NULL);
        size_t current_element = *elements_count - 1;

        do {
            if (kv_elements[current_element].ttl > 0) {
                if (current_time - kv_elements[current_element].created > kv_elements[current_element].ttl) {
                    for (size_t i = current_element; i < *elements_count - 1; ++i) {
                        kv_elements[i].key = kv_elements[i + 1].key;
                        kv_elements[i].value = kv_elements[i + 1].value;
                        kv_elements[i].ttl = kv_elements[i + 1].ttl;
                        kv_elements[i].created = kv_elements[i + 1].created;
                    }
                    --*elements_count;
                }
            }
            if (current_element == 0) {
                break;
            }
            --current_element;
        } while (true);
    }

    if (sem_post(stg_sem_id) == -1) {
        throw std::runtime_error("Unable to free the semaphore");
    }
}

int master_loop(int port)
{
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "ERROR: Unable to create epoll descriptor" << std::endl;
        return -2;
    }

    sem_t* stg_sem_id;
    int stg_shm_id;
    void* stg_shm_ptr;
    if (init_storage_main(&stg_sem_id, &stg_shm_id, &stg_shm_ptr) == -1) {
        close(epoll_fd);
        std::cerr << "ERROR: Unable to create shared resources" << std::endl;
        return -2;
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP | SO_REUSEADDR);
    if (server_socket == -1) {
        destroy_storage_main(stg_sem_id, stg_shm_id, stg_shm_ptr);
        close(epoll_fd);
        std::cerr << "ERROR: Unable to create server socket" << std::endl;
        return -2;
    }

    int signal_fd;
    if (setup_signals_main(&signal_fd) == -1) {
        destroy_storage_main(stg_sem_id, stg_shm_id, stg_shm_ptr);
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
        close(epoll_fd);
        std::cerr << "ERROR: Unable to create signal descriptor" << std::endl;
        return -2;
    }

    if (start_listening(epoll_fd, server_socket, signal_fd, port) == -1) {
        destroy_storage_main(stg_sem_id, stg_shm_id, stg_shm_ptr);
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
        close(signal_fd);
        close(epoll_fd);
        std::cerr << "ERROR: Unable to listen to the socket" << std::endl;
        return -2;
    }

    bool needs_exit = false;
    std::list<WorkerInfo> workers_list;

    create_workers(workers_list, epoll_fd, stg_sem_id, stg_shm_id);

    while (!needs_exit) {
        epoll_event events[EVENTS_MAX];
        int events_count = epoll_wait(epoll_fd, events, EVENTS_MAX, EPOLL_WAIT_TIMEOUT_MSEC);
        for (int i = 0; i < events_count; ++i) {
            if (events[i].data.fd == signal_fd) {
                signalfd_siginfo signal_info;
                ssize_t read_bytes = read(signal_fd, &signal_info, sizeof(signal_info));
                if (read_bytes == sizeof(signal_info)) {
                    if (signal_info.ssi_signo == SIGINT) {
                        needs_exit = true;
                    }
                } else {
                    needs_exit = true;
                }
            } else if (events[i].data.fd == server_socket) {
                client_accept(workers_list, server_socket);
            } else {
                for (auto worker_i = workers_list.begin(); worker_i != workers_list.end(); ++worker_i) {
                    if (worker_i->pass_fd == events[i].data.fd) {
                        int buffer;
                        read(worker_i->pass_fd, &buffer, sizeof(int));
                        worker_i->free = true;
                        break;
                    }
                }
            }
        }

        clean_old_entries(stg_sem_id, stg_shm_ptr);
    }

    clear_epoll_main(epoll_fd, server_socket, signal_fd);

    wait_for_workers(workers_list, epoll_fd);

    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);

    close(signal_fd);

    destroy_storage_main(stg_sem_id, stg_shm_id, stg_shm_ptr);

    close(epoll_fd);

    return 0;
}
