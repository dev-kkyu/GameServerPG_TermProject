#include "GameFramework.h"

int GameFramework::getNewClientID()
{
	for (int i = 0; i < MAX_USER; ++i) {
		std::lock_guard<std::mutex> ll{ objects[i].socket_lock };
		if (objects[i].state == Session::ST_FREE) {
			objects[i].state = Session::ST_ALLOC;
			return i;
		}
	}
	return -1;
}

void GameFramework::clientStart(int c_id, ::SOCKET c_socket)
{
	objects[c_id].x = 0;
	objects[c_id].y = 0;
	objects[c_id].id = c_id;
	objects[c_id].name[0] = '\0';
	objects[c_id].prev_remain = 0;
	objects[c_id].socket = c_socket;
	//objects[c_id].sec_idx = { -1, -1 };
	objects[c_id].doRecv();
}

void GameFramework::processRecv(int c_id, int recv_size)
{
	int remain_data = recv_size + objects[c_id].prev_remain;
	EXP_OVERLAPPED* exp_over = &objects[c_id].recv_over;
	char* p = exp_over->rw_buf;
	// Todo : ��Ŷ �������� short������ �ٲ���� �Ѵ�.
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
	// Todo : Ŭ���̾�Ʈ ������ closesocket ������ ���� ���� ����
}

void GameFramework::processPacket(int c_id, char* packet)
{
}
