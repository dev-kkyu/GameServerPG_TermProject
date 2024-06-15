#pragma once

#include <mutex>

#include "EXP_OVERLAPPED.h"		// 확장 WSAOVERLAPPED 클래스

class Session
{
public:
	enum STATE { ST_FREE, ST_ALLOC, ST_INGAME };

public:
	EXP_OVERLAPPED recv_over;

	::SOCKET socket;
	std::mutex socket_lock;

	STATE state;

	int id;
	short x, y;
	char	name[NAME_SIZE];

	int		prev_remain;

	int		last_move_time;

public:
	Session();

	void doRecv();
	void doSend(void* packet);

};

