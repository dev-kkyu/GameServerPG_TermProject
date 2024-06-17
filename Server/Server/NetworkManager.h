#pragma once

#include <WS2tcpip.h>

#include "GameFramework.h"

class NetworkManager
{
private:
	::SOCKET server_socket;
	::SOCKET accept_socket;
	EXP_OVERLAPPED accept_over{ EXP_OVERLAPPED::OP_ACCEPT };

	::HANDLE handle_iocp;

	GameFramework gameFramework;

public:

	NetworkManager(unsigned short port);
	~NetworkManager();

	void run();

};

