#define MAX_CLIENTS 64
#define MAX_MESSAGE_SIZE 1024
#define MAX_WORD_LENGTH 100
#define SERVER_PORT 13666
#define FILE_CHECK_PERIOD_MSEC 60000

#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <list>
#include <string>
#include <stdexcept>

#include <condition_variable>
#include <thread>

#include <pthread.h>

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

class WordsServer
{
public:
    WordsServer(std::string words_filename, int port);
    ~WordsServer();

    void main_thread_loop();

private:
    void accept_client();
    void disconnect_client(int client_socket);
    std::thread *receive_client_prefix(int client_socket);
    std::list<std::string> get_words_with_prefix(const std::string& prefix) const;
    static void worker_thread_routine(WordsServer* server, std::string word, int client_socket);
    void update_dict_if_necessary(bool throw_exceptions);


    std::string _words_filename;
    std::list<std::string> _words_dict;
    mutable pthread_rwlock_t _words_lock;

    int _port;
    int _server_socket;
    int _epoll_fd;

    time_t _last_words_check_time;
};

WordsServer::WordsServer(std::string words_filename, int port):
    _words_filename(words_filename), _port(port), _last_words_check_time(0)
{
    pthread_rwlockattr_t attributes;

    if (pthread_rwlockattr_init(&attributes) != 0) {
        throw std::runtime_error("Unable to create rwlock attributes.");
    }
    if (pthread_rwlock_init(&this->_words_lock, &attributes) != 0) {
        pthread_rwlockattr_destroy(&attributes);
        throw std::runtime_error("Unable to create rwlock.");
    }

    pthread_rwlockattr_destroy(&attributes);

    this->_epoll_fd = epoll_create1(0);
    if (this->_epoll_fd == -1) {
        pthread_rwlock_destroy(&this->_words_lock);
        throw std::runtime_error("Unable to create epoll descriptor.");
    }

    try {
        this->update_dict_if_necessary(true);
    }
    catch (...) {
        pthread_rwlock_destroy(&this->_words_lock);
        close(this->_epoll_fd);
        throw std::runtime_error("Unable load words file.");
    }

    this->_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP | SO_REUSEADDR);
    if (this->_server_socket == -1) {
        pthread_rwlock_destroy(&this->_words_lock);
        close(this->_epoll_fd);
        throw std::runtime_error("Unable to create socket.");
    }

    sockaddr_in socket_address;
    socket_address.sin_addr.s_addr = htonl(INADDR_ANY);
    socket_address.sin_port = htons(this->_port);
    socket_address.sin_family = AF_INET;

    if (bind(this->_server_socket, (sockaddr*)&socket_address, sizeof(socket_address)) == -1) {
        shutdown(this->_server_socket, SHUT_RDWR);
        close(this->_server_socket);
        pthread_rwlock_destroy(&this->_words_lock);
        close(this->_epoll_fd);
        throw std::runtime_error("Unable to bind socket.");
    }

    if (listen(this->_server_socket, MAX_CLIENTS) == -1) {
        shutdown(this->_server_socket, SHUT_RDWR);
        close(this->_server_socket);
        pthread_rwlock_destroy(&this->_words_lock);
        close(this->_epoll_fd);
        throw std::runtime_error("Unable to listen socket.");
    }

    epoll_event event;
    event.data.fd = this->_server_socket;
    event.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(this->_epoll_fd, EPOLL_CTL_ADD, this->_server_socket, &event) == -1) {
        shutdown(this->_server_socket, SHUT_RDWR);
        close(this->_server_socket);
        pthread_rwlock_destroy(&this->_words_lock);
        close(this->_epoll_fd);
        throw std::runtime_error("Unable to epoll socket.");
    }
}

WordsServer::~WordsServer()
{
    epoll_event event;
    event.data.fd = this->_server_socket;
    event.events = EPOLLIN | EPOLLET;

    epoll_ctl(this->_epoll_fd, EPOLL_CTL_DEL, this->_server_socket, &event);

    shutdown(this->_server_socket, SHUT_RDWR);
    close(this->_server_socket);
    this->_server_socket = -1;

    close(this->_epoll_fd);
    this->_epoll_fd = -1;

    pthread_rwlock_destroy(&this->_words_lock);
}

void WordsServer::accept_client()
{
    int client_socket = accept(this->_server_socket, NULL, NULL);

    if (client_socket > 0) {
        epoll_event client_event;
        client_event.data.fd = client_socket;
        client_event.events = EPOLLIN | EPOLLET;

        if (epoll_ctl(this->_epoll_fd, EPOLL_CTL_ADD, client_socket, &client_event) == -1) {
            shutdown(client_socket, SHUT_RDWR);
            close(client_socket);
        }
    }
}

void WordsServer::disconnect_client(int client_socket)
{
    epoll_event event;
    event.data.fd = client_socket;
    event.events = EPOLLIN | EPOLLET;

    epoll_ctl(this->_epoll_fd, EPOLL_CTL_DEL, client_socket, &event);

    shutdown(client_socket, SHUT_RDWR);
    close(client_socket);
}

std::thread* WordsServer::receive_client_prefix(int client_socket)
{
    char buffer[MAX_MESSAGE_SIZE];
    std::string word;

    ssize_t received_bytes = recv(client_socket, &buffer, MAX_MESSAGE_SIZE, MSG_NOSIGNAL);
    if (received_bytes > 0) {
        bool has_le = false;
        size_t i = 0;
        for (; i < MAX_MESSAGE_SIZE; ++i) {
            if (buffer[i] == '\n') {
                buffer[i] = '\0';
                has_le = true;
                break;
            }
        }

        if (!has_le) {
            this->disconnect_client(client_socket);
        }

        word = std::string(buffer);
        return new std::thread(WordsServer::worker_thread_routine, this, word, client_socket);
    } else {
        this->disconnect_client(client_socket);
        return nullptr;
    }
}

std::list<std::string> WordsServer::get_words_with_prefix(const std::string& prefix) const
{
    std::list<std::string> words;

    if (pthread_rwlock_rdlock(&this->_words_lock) != 0) {
        return words;
    }

    auto word_i = this->_words_dict.cbegin();
    while (word_i != this->_words_dict.cend()) {
        if (*word_i >= prefix) {
            break;
        }
        ++word_i;
    }

    if (word_i != this->_words_dict.cend()) {
        for (int i = 0; i < 10; ++i) {
            if (word_i->size() < prefix.size()) {
                break;
            }
            if (word_i->substr(0, prefix.size()) != prefix) {
                break;
            }
            words.push_back(*word_i);
            ++word_i;
        }
    }

    pthread_rwlock_unlock(&this->_words_lock);

    return words;
}

void WordsServer::main_thread_loop()
{
    epoll_event events[MAX_CLIENTS];
    std::list<std::thread*> requests_threads;

    int events_count = epoll_wait(this->_epoll_fd, events, MAX_CLIENTS, FILE_CHECK_PERIOD_MSEC);
    if (events_count == -1) {
        throw std::runtime_error("Main loop epoll wait fail.");
    }

    for (int i = 0; i < events_count; ++i) {
        if (events[i].data.fd == this->_server_socket) {
            this->accept_client();
        } else {
            auto child_thread = this->receive_client_prefix(events[i].data.fd);
            if (child_thread) {
                requests_threads.push_back(child_thread);
            }
        }
    }

    while (requests_threads.size() > 0) {
        auto request_thread = requests_threads.front();
        request_thread->join();
        requests_threads.pop_front();
        delete request_thread;
    }

    this->update_dict_if_necessary(false);
}

void WordsServer::worker_thread_routine(WordsServer *server, std::string word, int client_socket)
{
    auto words = server->get_words_with_prefix(word);
    std::string message;
    std::string el_string = std::string("\n");
    if (words.size() > 0) {
        for (auto word_i = words.begin(); word_i != words.end(); ++word_i) {
            message += *word_i + el_string;
        }
    } else {
        message = el_string;
    }

    char buffer[MAX_MESSAGE_SIZE];
    strncpy(buffer, message.c_str(), message.size());
    send(client_socket, buffer, message.size(), MSG_NOSIGNAL);
}

void WordsServer::update_dict_if_necessary(bool throw_exceptions)
{
    struct stat attributes;
    if (stat(this->_words_filename.c_str(), &attributes) == -1) {
        if (throw_exceptions) {
            throw std::runtime_error("Specified file doesn't exist.");
        }
        return;
    }
    if (attributes.st_mtime > this->_last_words_check_time) {
        std::ifstream words_file_stream(this->_words_filename);
        std::string word;

        if (pthread_rwlock_wrlock(&this->_words_lock) != 0) {
            if (throw_exceptions) {
                throw std::runtime_error("Unable to lock rwlock for dictionary upgrade.");
            }
            return;
        }
        this->_words_dict.clear();

        while (std::getline(words_file_stream, word)) {
            if ((word.size() > 0) && (word.size() <= MAX_WORD_LENGTH)) {
                this->_words_dict.push_back(word);
            }
        }

        this->_words_dict.sort();
        this->_last_words_check_time = std::time(NULL);
        pthread_rwlock_unlock(&this->_words_lock);

        words_file_stream.close();
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Need words dictionary filename." << std::endl;
        return 1;
    }

    WordsServer* server;
    try {
        server = new WordsServer(std::string(argv[1]), SERVER_PORT);
    }
    catch (...) {
        std::cerr << "Unable to create server instance." << std::endl;
        return 2;
    }


    while (1) {
        server->main_thread_loop();
    }

    delete server;

    return 0;
}
