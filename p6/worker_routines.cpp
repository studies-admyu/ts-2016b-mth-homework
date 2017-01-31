#include <cstring>
#include <ctime>
#include <sstream>
#include <string>
#include <stdexcept>

#include <unistd.h>
#include <semaphore.h>
#include <signal.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/mman.h>

#include "routines.hpp"

#define MESSAGE_SIZE_MAX 512

extern const char* posix_shm_name;
extern const char* posix_sem_name;

int setup_signals_worker(int* signal_fd)
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

int start_waiting_for_fd(int epoll_fd, int pass_fd, int signal_fd)
{
    epoll_event events[2];
    events[0].data.fd = pass_fd;
    events[0].events = EPOLLIN | EPOLLET;

    events[1].data.fd = signal_fd;
    events[1].events = EPOLLIN | EPOLLET;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pass_fd, &(events[0])) != 0) {
        return -1;
    }

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, signal_fd, &(events[1])) != 0) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pass_fd, &(events[0]));
        return -1;
    }

    return 0;
}

void clear_epoll_worker(int epoll_fd, int pass_fd, int signal_fd)
{
    epoll_event events[2];
    events[0].data.fd = pass_fd;
    events[0].events = EPOLLIN | EPOLLET;

    events[1].data.fd = signal_fd;
    events[1].events = EPOLLIN | EPOLLET;

    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, pass_fd, &(events[0]));
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, signal_fd, &(events[1]));
}

int notify_free(int pass_fd)
{
    int buffer = 0;
    if (send(pass_fd, &buffer, sizeof(int), 0) != sizeof(int)) {
        return -1;
    }
    return 0;
}

int start_reading_client(int epoll_fd, int client_socket)
{
    epoll_event event;
    event.data.fd = client_socket;
    event.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event) != 0) {
        return -1;
    }

    return 0;
}

int disconnect_client(int epoll_fd, int client_socket)
{
    epoll_event event;
    event.data.fd = client_socket;
    event.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, &event) != 0) {
        return -1;
    }

    shutdown(client_socket, SHUT_RDWR);
    close(client_socket);

    return 0;
}

int init_storage_worker(int stg_shm_id, void** stg_shm_ptr)
{
    *stg_shm_ptr = mmap(NULL, STORAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, stg_shm_id, 0);
    if (!(*stg_shm_ptr)) {
        return -1;
    }

    return 0;
}

void destroy_storage_worker(sem_t* stg_sem_id, int stg_shm_id, void* stg_shm_ptr)
{
    munmap(stg_shm_ptr, STORAGE_SIZE);
    shm_unlink(posix_shm_name);
    close(stg_shm_id);

    sem_unlink(posix_sem_name);
    sem_close(stg_sem_id);
}

bool run_command(std::string command, int key, int ttl, int* value, sem_t* stg_sem_id, void* stg_shm_ptr)
{
    bool result = false;

    if (sem_wait(stg_sem_id) == -1) {
        throw std::runtime_error("Unable to lock the semaphore");
    }

    size_t* elements_count= (size_t*)stg_shm_ptr;
    KeyValuePair* kv_elements = (KeyValuePair*)(elements_count + 1);

    bool entry_found = false;
    size_t entry_index = 0;
    for (size_t i = 0; i < *elements_count; ++i) {
        if (kv_elements[i].key == key) {
            entry_found = true;
            entry_index = i;
            break;
        }
    }

    if (command == std::string("set")) {
        if (entry_found) {
            kv_elements[entry_index].value = *value;
            kv_elements[entry_index].ttl = ttl;
            kv_elements[entry_index].created = time(NULL);
            result = true;
        } else if (*elements_count < STORAGE_ELEMENTS_MAX) {
            kv_elements[*elements_count].key = key;
            kv_elements[*elements_count].value = *value;
            kv_elements[*elements_count].ttl = ttl;
            kv_elements[*elements_count].created = time(NULL);
            *elements_count += 1;
            result = true;
        } else {
            result = false;
        }
    } else if (command == std::string("get")) {
        if (entry_found) {
            *value = kv_elements[entry_index].value;
            result = true;
        } else {
            result = false;
        }
    } else {
        if (entry_found) {
            for (size_t i = entry_index; i < *elements_count - 1; ++i) {
                kv_elements[i].key = kv_elements[i + 1].key;
                kv_elements[i].value = kv_elements[i + 1].value;
            }
            *elements_count -= 1;
            result = true;
        } else {
            result = false;
        }
    }

    if (sem_post(stg_sem_id) == -1) {
        throw std::runtime_error("Unable to free the semaphore");
    }

    return result;
}

bool read_client_data(int client_socket, sem_t* stg_sem_id, void* stg_shm_ptr) {
    char buffer[MESSAGE_SIZE_MAX + 1];
    ssize_t received_bytes = recv(client_socket, buffer, MESSAGE_SIZE_MAX, MSG_NOSIGNAL);
    if (received_bytes > 0) {
        buffer[received_bytes] = '\0';
        std::string message(buffer);
        std::string return_message;
        std::istringstream message_stream(message);

        std::string command;
        int key;
        int value;
        int ttl = 0;
        int value_to_get;

        message_stream >> command;
        if (command == std::string("set")) {
            message_stream >> ttl;
            message_stream >> key;
            message_stream >> value;
        } else if ((command == std::string("get")) ||  (command == std::string("delete"))) {
            message_stream >> key;
        } else {
            return false;
        }

        if (command == std::string("set")) {
            if (!run_command(command, key, ttl, &value, stg_sem_id, stg_shm_ptr)) {
                return_message = std::string("Unable to add key\n");
            } else {
                return_message = std::string("OK\n");
            }
        } else if (command == std::string("get")) {
            if (!run_command(command, key, ttl, &value_to_get, stg_sem_id, stg_shm_ptr)) {
                return_message = std::string("No such a key\n");
            } else {
                return_message = std::to_string(value_to_get) + std::string("\n");
            }
        } else if (command == std::string("delete")) {
            if (!run_command(command, key, ttl, &value_to_get, stg_sem_id, stg_shm_ptr)) {
                return_message = std::string("No such a key\n");
            } else {
                return_message = std::string("OK\n");
            }
        } else {
            return false;
        }

        std::strncpy(buffer, return_message.c_str(), return_message.size());
        send(client_socket, buffer, return_message.size(), MSG_NOSIGNAL);
        return true;
    } else {
        return false;
    }
}

int worker_loop(int pass_fd, sem_t* stg_sem_id, int stg_shm_id)
{
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        close(pass_fd);
        return -1;
    }

    int signal_fd;
    if (setup_signals_worker(&signal_fd) == -1) {
        close(pass_fd);
        close(epoll_fd);
        return -1;
    }

    if (start_waiting_for_fd(epoll_fd, pass_fd, signal_fd) == -1) {
        close(pass_fd);
        close(signal_fd);
        close(epoll_fd);
        return -1;
    }

    void* stg_shm_ptr = NULL;
    if (init_storage_worker(stg_shm_id, &stg_shm_ptr) == -1) {
        clear_epoll_worker(epoll_fd, pass_fd, signal_fd);
        close(pass_fd);
        close(signal_fd);
        close(epoll_fd);
        return -1;
    }

    bool needs_exit = false;
    int client_socket = -1;

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
            } else if (events[i].data.fd == pass_fd) {
                if (pass_fd_read(pass_fd, &client_socket) == 0) {
                    if (client_socket >= 0) {
                        if (start_reading_client(epoll_fd, client_socket) == -1) {
                            shutdown(client_socket, SHUT_RDWR);
                            close(client_socket);
                            client_socket = -1;
                        }
                    }
                }
            } else if (events[i].data.fd == client_socket) {
                if (!read_client_data(client_socket, stg_sem_id, stg_shm_ptr)) {
                    disconnect_client(epoll_fd, client_socket);
                    client_socket = -1;
                    notify_free(pass_fd);
                }
            }
        }
    }

    if (client_socket >= 0) {
        disconnect_client(epoll_fd, client_socket);
        client_socket = -1;
    }

    destroy_storage_worker(stg_sem_id, stg_shm_id, stg_shm_ptr);
    stg_shm_ptr = NULL;

    clear_epoll_worker(epoll_fd, pass_fd, signal_fd);

    close(pass_fd);

    close(signal_fd);
    close(epoll_fd);

    return 0;
}
