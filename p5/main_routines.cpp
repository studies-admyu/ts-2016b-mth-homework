#include <iostream>
#include <list>

#include <unistd.h>
#include <wait.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include <netinet/in.h>

#include "routines.hpp"

#define CLIENTS_MAX 64

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

    if (listen(server_socket, CLIENTS_MAX) == -1) {
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

int init_storage_main(int* stg_sem_id, int* stg_shm_id)
{
    key_t storage_key = ftok("./storage", 10);

    *stg_shm_id = shmget(storage_key, STORAGE_SIZE, 0666 | IPC_CREAT);
    if (*stg_shm_id == -1) {
        return -1;
    }
    *stg_sem_id = semget(storage_key, 1, 0666 | IPC_CREAT);
    if (*stg_sem_id == -1) {
        shmctl(*stg_shm_id, IPC_RMID, NULL);
        return -1;
    }

    size_t* entries_count = (size_t*)shmat(*stg_shm_id, NULL, 0);
    *entries_count = 0;
    return shmdt(entries_count);
}

void destroy_storage_main(int stg_sem_id, int stg_shm_id) {
    shmctl(stg_shm_id, IPC_RMID, NULL);
    semctl(stg_sem_id, 0, IPC_RMID);
}

void client_accept(std::list<pid_t>& pids, int server_socket, int stg_sem_id, int stg_shm_id)
{
    int client_socket = accept(server_socket, NULL, 0);
    if (client_socket == -1) {
        return;
    }

    pid_t worker_pid;

    if ((worker_pid = fork()) == 0) {
        pids.clear();
        exit(worker_loop(client_socket, stg_sem_id, stg_shm_id));
    } else {
        pids.push_back(worker_pid);
    }
}

int master_loop(int port)
{
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "ERROR: Unable to create epoll descriptor" << std::endl;
        return -2;
    }

    int stg_sem_id;
    int stg_shm_id;
    if (init_storage_main(&stg_sem_id, &stg_shm_id) == -1) {
        close(epoll_fd);
        std::cerr << "ERROR: Unable to create shared resources" << std::endl;
        return -2;
    }

    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP | SO_REUSEADDR);
    if (server_socket == -1) {
        destroy_storage_main(stg_sem_id, stg_shm_id);
        close(epoll_fd);
        std::cerr << "ERROR: Unable to create server socket" << std::endl;
        return -2;
    }

    int signal_fd;
    if (setup_signals_main(&signal_fd) == -1) {
        destroy_storage_main(stg_sem_id, stg_shm_id);
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
        close(epoll_fd);
        std::cerr << "ERROR: Unable to create signal descriptor" << std::endl;
        return -2;
    }

    if (start_listening(epoll_fd, server_socket, signal_fd, port) == -1) {
        destroy_storage_main(stg_sem_id, stg_shm_id);
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
        close(signal_fd);
        close(epoll_fd);
        std::cerr << "ERROR: Unable to listen to the socket" << std::endl;
        return -2;
    }

    bool needs_exit = false;
    std::list<pid_t> children_pids;

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
                client_accept(children_pids, server_socket, stg_sem_id, stg_shm_id);
            }
        }

        pid_t zombie_pid = waitpid(-1, NULL, WNOHANG);
        while (zombie_pid > 0) {
            for (auto pid_i = children_pids.begin(); pid_i != children_pids.end(); ++pid_i) {
                if (*pid_i == zombie_pid) {
                    children_pids.erase(pid_i, std::next(pid_i));
                    break;
                }
            }

            zombie_pid = waitpid(-1, NULL, WNOHANG);
        }

    }

    clear_epoll_main(epoll_fd, server_socket, signal_fd);

    while (children_pids.size() > 0) {
        pid_t zombie_pid = waitpid(-1, NULL, 0);

        for (auto pid_i = children_pids.begin(); pid_i != children_pids.end(); ++pid_i) {
            if (*pid_i == zombie_pid) {
                children_pids.erase(pid_i, std::next(pid_i));
                break;
            }
        }
    }

    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);

    close(signal_fd);

    destroy_storage_main(stg_sem_id, stg_shm_id);

    close(epoll_fd);

    return 0;
}
