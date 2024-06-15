#include "GameFramework.h"

void GameFramework::processRecv(int c_id, int recv_size)
{
	int remain_data = recv_size + objects[c_id].prev_remain;
	EXP_OVERLAPPED* exp_over = &objects[c_id].recv_over;
	char* p = exp_over->rw_buf;
	// Todo : 패킷 재조립을 short용으로 바꿔줘야 한다.
	while (remain_data > 0) {
		int packet_size = reinterpret_cast<unsigned short*>(p)[0];
		if (packet_size <= remain_data) {
			processPacket(c_id, p);
			p = p + packet_size;
			remain_data = remain_data - packet_size;
		}
		else break;
	}
	objects[c_id].prev_remain = remain_data;
	if (remain_data > 0) {
		memcpy(exp_over->rw_buf, p, remain_data);
	}
	objects[c_id].doRecv();
}

void GameFramework::disconnect(int c_id)
{
}

void GameFramework::processPacket(int c_id, char* packet)
{
}
