#pragma once

#include <mutex>

#include "EXP_OVERLAPPED.h"

class Session
{
public:
	enum STATE { ST_FREE, ST_ALLOC, ST_INGAME };

private:
	EXP_OVERLAPPED recv_over;

public:
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

