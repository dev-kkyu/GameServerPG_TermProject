#pragma once

// 확장된 WSAOVERLAPPED 클래스

#include <WS2tcpip.h>
#include "protocol_2024.h"

class EXP_OVERLAPPED
{
public:
	enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPC_MOVE, OP_PLAYER_MOVE, OP_AI_BYE, OP_RESPAWN, OP_HP_CHARGE, OP_DB_LOGIN };

public:
	::WSAOVERLAPPED wsaover;		// 첫번째 변수로 넣음으로써 확장 클래스의 시작 주소와 일치시킨다
	::WSABUF wsabuf;
	char rw_buf[BUF_SIZE];
	COMP_TYPE comp_type;
	int ai_target_obj;

public:
	EXP_OVERLAPPED(COMP_TYPE c_type);		// Accept, Recv용
	EXP_OVERLAPPED(void* packet);					// Send용

};

class EXP_EXP_OVER
{
public:
	EXP_OVERLAPPED exp_over;

	// id는 rw_buf에
	short x, y;
	short level;
	int max_hp;
	int hp;
	int exp;

public:
	EXP_EXP_OVER(const char* name, short x, short y, short level, int max_hp, int hp, int exp);
};
