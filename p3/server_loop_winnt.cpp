#include "server_loop.hpp"

#include <windows.h>

void (*server_shutdown_function)() = nullptr;

HANDLE hCleanupCompleted;

BOOL WINAPI console_handler(DWORD dwCtrlType)
{
	UNREFERENCED_PARAMETER(dwCtrlType);

	server_shutdown_function();
	WaitForSingleObject(hCleanupCompleted, INFINITE);

	CloseHandle(hCleanupCompleted);

	return TRUE;
}

bool init_serv_int(void (*shutdown_function)())
{
	if (!shutdown_function) {
		return false;
	}
	server_shutdown_function = shutdown_function;

	hCleanupCompleted = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!hCleanupCompleted) {
		return false;
	}
	if (SetConsoleCtrlHandler(console_handler, TRUE) != TRUE) {
		CloseHandle(hCleanupCompleted);
		return false;
	}
	return true;
}

void cleanup_serv_int()
{
	SetEvent(hCleanupCompleted);
}
