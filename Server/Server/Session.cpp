#include "Session.h"

Session::Session()
	: recv_over{ EXP_OVERLAPPED::OP_RECV }, name{}
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
