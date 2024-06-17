#include "GameFramework.h"

std::pair<int, int> SECTOR::getSectorIndex(int pos_x, int pos_y)
{
	int ret_x = pos_x / GameFramework::SECTOR_RANGE;
	int ret_y = pos_y / GameFramework::SECTOR_RANGE;
	return { ret_x, ret_y };
}

GameFramework::GameFramework(const HANDLE& h_iocp)
	: iocp_handle{ h_iocp }
{
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
	// ���Ϳ� ��ġ�� �־�� ���������� �÷��� ������ ���� (�α��� ���н� ó��X)
	if (objects[c_id].sec_idx.first >= 0 and objects[c_id].sec_idx.second >= 0) {
		// ���Ϳ��� ����
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

		// DB�� ����
		//auto ptr = std::make_shared<DB_EVENT_SAVE>(c_id);
		//db_queue.push(ptr);		// DBť�� �־ �̸��� Ȯ���� �ش�...
	}

	std::lock_guard<std::mutex> ll(objects[c_id].socket_lock);
	objects[c_id].state = Session::ST_FREE;
}

void GameFramework::tryNpcMove(int npc_id)
{
	//bool keep_alive = false;
	for (int j = 0; j < MAX_USER; ++j) {
		if (objects[j].state != Session::ST_INGAME) continue;
		if (can_see(npc_id, j)) {
			//keep_alive = true;
			doNpcRandomMove(npc_id);		// ���⼭ �ٷ� ���ش� (�ѹ��� ����)
			break;
		}
	}
	//if (true == keep_alive) {
	//	do_npc_random_move(static_cast<int>(key));
	//	TIMER_EVENT ev{ key, chrono::system_clock::now() + 1s, EV_RANDOM_MOVE, 0 };
	//	timer_queue.push(ev);
	//}
	//else {
	//	clients[key]._is_active = false;
	//}
}

void GameFramework::processPacket(int c_id, char* packet)
{
	switch (packet[1]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);

		//auto ptr = std::make_shared<DB_EVENT_LOGIN>(c_id, p->name);
		//db_queue.push(ptr);		// DBť�� �־ �̸��� Ȯ���� �ش�...

		break;
	}
	case CS_MOVE: {
		if (objects[c_id].sec_idx.first < 0 or objects[c_id].sec_idx.second < 0) {
			closesocket(objects[c_id].socket);	// �α����� ���� ���� Ŭ���̾�Ʈ�� �������� �����Ǹ� ��ü���� close�Ѵ�.
			break;
		}
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		objects[c_id].last_move_time = p->move_time;
		short x = objects[c_id].x;
		short y = objects[c_id].y;
		switch (p->direction) {
		case 0: if (y > 0) y--; break;
		case 1: if (y < W_HEIGHT - 1) y++; break;
		case 2: if (x > 0) x--; break;
		case 3: if (x < W_WIDTH - 1) x++; break;
		}
		objects[c_id].x = x;
		objects[c_id].y = y;

		// �� ��ǥ�� ���� ���� ��ġ�� �ٲ������, �ٲ��ش�
		std::pair<int, int> bef_sec = objects[c_id].sec_idx;
		std::pair<int, int> aft_sec = SECTOR::getSectorIndex(x, y);
		if (bef_sec != aft_sec) {
			sectors[bef_sec.first][bef_sec.second].sector_m.lock();
			sectors[bef_sec.first][bef_sec.second].object_list.erase(c_id);
			sectors[bef_sec.first][bef_sec.second].sector_m.unlock();

			sectors[aft_sec.first][aft_sec.second].sector_m.lock();
			sectors[aft_sec.first][aft_sec.second].object_list.insert(c_id);
			sectors[aft_sec.first][aft_sec.second].sector_m.unlock();

			objects[c_id].sec_idx = aft_sec;
		}

		std::unordered_set<int> near_list;
		objects[c_id].view_lock.lock();
		std::unordered_set<int> old_vlist = objects[c_id].view_list;
		objects[c_id].view_lock.unlock();

		// �¿���� 1���;� �� ����, �� ��ó�� �� 9�� ���͸� ���Ѵ�
		for (int i = aft_sec.first - 1; i <= aft_sec.first + 1; ++i) {
			if (i < 0 or i > W_WIDTH / SECTOR_RANGE - 1)	// ������ ��� ���� Ž������ �ʴ´�
				continue;
			for (int j = aft_sec.second - 1; j <= aft_sec.second + 1; ++j) {
				if (j < 0 or j > W_HEIGHT / SECTOR_RANGE - 1)
					continue;
				sectors[i][j].sector_m.lock();
				std::unordered_set<int> sec_lists = sectors[i][j].object_list;
				sectors[i][j].sector_m.unlock();

				// �� �丮��Ʈ ����
				for (auto p_id : sec_lists) {
					if (objects[p_id].state != Session::ST_INGAME) continue;
					if (false == can_see(c_id, p_id)) continue;
					if (p_id == c_id) continue;
					near_list.insert(p_id);
				}
			}
		}

		//for (auto& cl : objects) {
		//	if (cl._state != ST_INGAME) continue;
		//	if (cl._id == c_id) continue;
		//	if (can_see(c_id, cl._id))
		//		near_list.insert(cl._id);
		//}

		objects[c_id].send_move_packet(objects[c_id]);

		for (auto& pl : near_list) {
			auto& cpl = objects[pl];
			if (is_pc(pl)) {
				cpl.view_lock.lock();
				if (objects[pl].view_list.count(c_id)) {
					cpl.view_lock.unlock();
					objects[pl].send_move_packet(objects[c_id]);
				}
				else {
					cpl.view_lock.unlock();
					objects[pl].send_add_player_packet(objects[c_id]);
				}
			}
			else wakeUpNPC(pl, c_id);

			if (old_vlist.count(pl) == 0)
				objects[c_id].send_add_player_packet(objects[pl]);
		}

		for (auto& pl : old_vlist) {
			if (0 == near_list.count(pl)) {
				objects[c_id].send_remove_player_packet(pl);
				if (is_pc(pl))
					objects[pl].send_remove_player_packet(c_id);
			}
		}
	}
				break;
	}
}

void GameFramework::wakeUpNPC(int npc_id, int waker)
{
	EXP_OVERLAPPED* exover = new EXP_OVERLAPPED{ EXP_OVERLAPPED::OP_PLAYER_MOVE };
	exover->ai_target_obj = waker;
	PostQueuedCompletionStatus(iocp_handle, 1, npc_id, &exover->wsaover);

	//if (clients[npc_id]._is_active) return;
	//bool old_state = false;
	//if (false == atomic_compare_exchange_strong(&clients[npc_id]._is_active, &old_state, true))
	//	return;
	//TIMER_EVENT ev{ npc_id, chrono::system_clock::now(), EV_RANDOM_MOVE, 0 };
	//timer_queue.push(ev);
}

void GameFramework::doNpcRandomMove(int npc_id)
{
	Session& npc = objects[npc_id];
	std::unordered_set<int> old_vl;
	for (auto& obj : objects) {						// �갡 �ֹ�, npc�� 20��.... ���� ������ �ʿ��ϴ�.
		if (Session::ST_INGAME != obj.state) continue;
		if (true == is_npc(obj.id)) continue;
		if (true == can_see(npc.id, obj.id))
			old_vl.insert(obj.id);
	}

	int x = npc.x;
	int y = npc.y;
	switch (rand() % 4) {
	case 0: if (x < (W_WIDTH - 1)) x++; break;
	case 1: if (x > 0) x--; break;
	case 2: if (y < (W_HEIGHT - 1)) y++; break;
	case 3:if (y > 0) y--; break;
	}
	npc.x = x;
	npc.y = y;

	// �� ��ǥ�� ���� ���� ��ġ�� �ٲ������, �ٲ��ش�
	std::pair<int, int> bef_sec = objects[npc_id].sec_idx;
	std::pair<int, int> aft_sec = SECTOR::getSectorIndex(x, y);
	if (bef_sec != aft_sec) {
		sectors[bef_sec.first][bef_sec.second].sector_m.lock();
		sectors[bef_sec.first][bef_sec.second].object_list.erase(npc_id);
		sectors[bef_sec.first][bef_sec.second].sector_m.unlock();

		sectors[aft_sec.first][aft_sec.second].sector_m.lock();
		sectors[aft_sec.first][aft_sec.second].object_list.insert(npc_id);
		sectors[aft_sec.first][aft_sec.second].sector_m.unlock();

		objects[npc_id].sec_idx = aft_sec;
	}

	// �¿���� 1���;� �� ����, �� ��ó�� �� 9�� ���͸� ���Ѵ�
	std::unordered_set<int> new_vl;
	for (int i = aft_sec.first - 1; i <= aft_sec.first + 1; ++i) {
		if (i < 0 or i > W_WIDTH / SECTOR_RANGE - 1)	// ������ ��� ���� Ž������ �ʴ´�
			continue;
		for (int j = aft_sec.second - 1; j <= aft_sec.second + 1; ++j) {
			if (j < 0 or j > W_HEIGHT / SECTOR_RANGE - 1)
				continue;
			sectors[i][j].sector_m.lock();
			std::unordered_set<int> sec_lists = sectors[i][j].object_list;
			sectors[i][j].sector_m.unlock();

			// �� �丮��Ʈ ����
			for (auto p_id : sec_lists) {
				if (objects[p_id].state != Session::ST_INGAME) continue;
				if (true == is_npc(p_id)) continue;
				if (false == can_see(npc_id, p_id)) continue;
				new_vl.insert(p_id);
			}
		}
	}

	//unordered_set<int> new_vl;
	//for (auto& obj : clients) {
	//	if (ST_INGAME != obj._state) continue;
	//	if (true == is_npc(obj._id)) continue;
	//	if (true == can_see(npc._id, obj._id))
	//		new_vl.insert(obj._id);
	//}

	for (auto pl : new_vl) {
		if (0 == old_vl.count(pl)) {
			// �÷��̾��� �þ߿� ����
			objects[pl].send_add_player_packet(npc);
		}
		else {
			// �÷��̾ ��� ���� ����.
			objects[pl].send_move_packet(npc);
		}
	}
	///vvcxxccxvvdsvdvds
	for (auto pl : old_vl) {
		if (0 == new_vl.count(pl)) {
			objects[pl].view_lock.lock();
			if (0 != objects[pl].view_list.count(npc.id)) {
				objects[pl].view_lock.unlock();
				objects[pl].send_remove_player_packet(npc.id);
			}
			else {
				objects[pl].view_lock.unlock();
			}
		}
	}
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
