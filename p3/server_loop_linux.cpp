#include "server_loop.hpp"

#include <signal.h>
#include <unistd.h>

void (*server_shutdown_function)() = nullptr;

void sigint_handler(int sig)
{
	if (sig == SIGINT) {
		server_shutdown_function();
	}
}

bool init_serv_int(void (*shutdown_function)())
{
	if (!shutdown_function) {
		return false;
	}
	server_shutdown_function = shutdown_function;
	
	if (signal(SIGINT, sigint_handler) == SIG_ERR) {
		return false;
	}
	return true;
}

void cleanup_serv_int()
{

}