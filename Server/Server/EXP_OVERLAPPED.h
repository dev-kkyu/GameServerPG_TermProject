#pragma once

// Ȯ��� WSAOVERLAPPED Ŭ����

#include <WS2tcpip.h>
#include "protocol_2024.h"

class EXP_OVERLAPPED
{
public:
	enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND };

public:
	::WSAOVERLAPPED wsaover;		// ù��° ������ �������ν� Ȯ�� Ŭ������ ���� �ּҿ� ��ġ��Ų��
	::WSABUF wsabuf;
	char rw_buf[BUF_SIZE];
	COMP_TYPE comp_type;

public:
	EXP_OVERLAPPED(COMP_TYPE c_type);		// Accept, Recv��
	EXP_OVERLAPPED(void* packet);					// Send��

};

