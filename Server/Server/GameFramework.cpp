#include "GameFramework.h"

std::pair<int, int> SECTOR::getSectorIndex(int pos_x, int pos_y)
{
	int ret_x = pos_x / GameFramework::SECTOR_RANGE;
	int ret_y = pos_y / GameFramework::SECTOR_RANGE;
	return { ret_x, ret_y };
}

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
	objects[c_id].sec_idx = { -1, -1 };
	objects[c_id].doRecv();
}

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
	// 섹터에 위치해 있어야 정상적으로 플레이 중으로 간주 (로그인 실패시 처리X)
	if (objects[c_id].sec_idx.first >= 0 and objects[c_id].sec_idx.second >= 0) {
		// 섹터에서 제거
		std::pair<int, int> sector = objects[c_id].sec_idx;
		sectors[sector.first][sector.second].sector_m.lock();
		sectors[sector.first][sector.second].object_list.erase(c_id);
		sectors[sector.first][sector.second].sector_m.unlock();
		objects[c_id].sec_idx = { -1, -1 };

		objects[c_id].view_lock.lock();
		std::unordered_set <int> vl = objects[c_id].view_list;
		objects[c_id].view_lock.unlock();
		for (auto& p_id : vl) {
			if (is_npc(p_id)) continue;
			auto& pl = objects[p_id];
			{
				std::lock_guard<std::mutex> ll(pl.socket_lock);
				if (Session::ST_INGAME != pl.state) continue;
			}
			if (pl.id == c_id) continue;
			pl.send_remove_player_packet(c_id);
		}
		closesocket(objects[c_id].socket);

		// DB에 저장
		//auto ptr = std::make_shared<DB_EVENT_SAVE>(c_id);
		//db_queue.push(ptr);		// DB큐에 넣어서 이름을 확인해 준다...
	}

	std::lock_guard<std::mutex> ll(objects[c_id].socket_lock);
	objects[c_id].state = Session::ST_FREE;
}

void GameFramework::processPacket(int c_id, char* packet)
{
}

bool GameFramework::is_pc(int object_id)
{
	return object_id < MAX_USER;
}

bool GameFramework::is_npc(int object_id)
{
	return !is_pc(object_id);
}

bool GameFramework::can_see(int from, int to)
{
	if (abs(objects[from].x - objects[to].x) > VIEW_RANGE) return false;
	return abs(objects[from].y - objects[to].y) <= VIEW_RANGE;
}
