#pragma once

#include <WS2tcpip.h>

#include "GameFramework.h"

#include <sqlext.h>

static void disp_error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode);
static bool db_error_check(SQLRETURN retcode);

class NetworkManager
{
private:
	::SOCKET server_socket;
	::SOCKET accept_socket;
	EXP_OVERLAPPED accept_over{ EXP_OVERLAPPED::OP_ACCEPT };

	::HANDLE handle_iocp;

	concurrency::concurrent_priority_queue<TIMER_EVENT> timer_queue;
	concurrency::concurrent_queue<std::shared_ptr<DB_EVENT>> db_queue;

	GameFramework gameFramework;

public:

	NetworkManager(unsigned short port);
	~NetworkManager();

	void runWorker();
	void runTimer();
	void runDB();

};

