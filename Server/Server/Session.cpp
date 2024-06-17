#include "Session.h"

#include "Dependencies/include/lua.hpp"
#pragma comment(lib, "Dependencies/lua54.lib")

Session::Session()
	: recv_over{ EXP_OVERLAPPED::OP_RECV }, name{}, ai_msg{}
{
	socket = 0;

	state = ST_FREE;

	id = -1;

	x = y = 0;

	prev_remain = 0;
	last_move_time = 0;
}

void Session::doRecv()
{
	::memset(&recv_over.wsaover, 0, sizeof(recv_over.wsaover));		// wsaover 구조체 재사용을 위한 초기화
	recv_over.wsabuf.len = BUF_SIZE - prev_remain;
	recv_over.wsabuf.buf = recv_over.rw_buf + prev_remain;

	::DWORD recv_flag = 0;
	::WSARecv(socket, &recv_over.wsabuf, 1, nullptr, &recv_flag, &recv_over.wsaover, nullptr);
}

void Session::doSend(void* packet)
{
	::EXP_OVERLAPPED* send_over = new EXP_OVERLAPPED{ packet };
	::WSASend(socket, &send_over->wsabuf, 1, nullptr, 0, &send_over->wsaover, nullptr);
}

void Session::send_login_info_packet()
{
	SC_LOGIN_INFO_PACKET p;
	p.id = id;
	p.size = sizeof(SC_LOGIN_INFO_PACKET);
	p.type = SC_LOGIN_INFO;
	p.x = x;
	p.y = y;
	doSend(&p);
}

void Session::send_login_fail_packet()
{
	SC_LOGIN_FAIL_PACKET p;
	p.size = sizeof(SC_LOGIN_FAIL_PACKET);
	p.type = SC_LOGIN_FAIL;
	doSend(&p);
}

void Session::send_move_packet(const Session& other)
{
	SC_MOVE_OBJECT_PACKET p;
	p.id = other.id;
	p.size = sizeof(SC_MOVE_OBJECT_PACKET);
	p.type = SC_MOVE_OBJECT;
	p.x = other.x;
	p.y = other.y;
	p.move_time = other.last_move_time;
	doSend(&p);
}

void Session::send_add_player_packet(const Session& other)
{
	SC_ADD_OBJECT_PACKET add_packet;
	add_packet.id = other.id;
	strcpy_s(add_packet.name, other.name);
	add_packet.size = sizeof(add_packet);
	add_packet.type = SC_ADD_OBJECT;
	add_packet.x = other.x;
	add_packet.y = other.y;
	view_lock.lock();
	view_list.insert(other.id);
	view_lock.unlock();
	doSend(&add_packet);
}

void Session::send_chat_packet(const Session& other, const char* mess)
{
	SC_CHAT_PACKET packet;
	packet.id = other.id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	strcpy_s(packet.mess, mess);
	doSend(&packet);
}

void Session::send_remove_player_packet(int c_id)
{
	view_lock.lock();
	if (view_list.count(c_id))
		view_list.erase(c_id);
	else {
		view_lock.unlock();
		return;
	}
	view_lock.unlock();
	SC_REMOVE_OBJECT_PACKET p;
	p.id = c_id;
	p.size = sizeof(p);
	p.type = SC_REMOVE_OBJECT;
	doSend(&p);
}
