#pragma once

// 확장된 WSAOVERLAPPED 클래스

#include <WS2tcpip.h>
#include "protocol_2024.h"

class EXP_OVERLAPPED
{
public:
	enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND };
public:
	::WSAOVERLAPPED wsaover;		// 첫번째 변수로 넣음으로써 확장 클래스의 시작 주소와 일치시킨다
	::WSABUF wsabuf;
	char rw_buf[BUF_SIZE];
	COMP_TYPE comp_type;

	EXP_OVERLAPPED(COMP_TYPE c_type, unsigned buf_len = BUF_SIZE);

};

