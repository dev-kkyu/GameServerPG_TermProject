#include "GameFramework.h"

#include <iostream>
#include <atomic>

#include "EVENTS.h"

#include "Dependencies/include/lua.hpp"
#pragma comment(lib, "Dependencies/lua54.lib")

GameFramework* GameFramework::instance = nullptr;

bool isMoveAble(short x, short y)		// ��ֹ�
{
	if (x % 5 == 0 and y % 5 == 0)
		return false;
	return true;
}

std::pair<int, int> SECTOR::getSectorIndex(int pos_x, int pos_y)
{
	int ret_x = pos_x / GameFramework::SECTOR_RANGE;
	int ret_y = pos_y / GameFramework::SECTOR_RANGE;
	return { ret_x, ret_y };
}

int GameFramework::API_get_x(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);	// ���ڷ� ������
	lua_pop(L, 2);					// ��ƿ��� �ø� �Լ��� �Լ��� ���ڰ� �� 2��
	int x = instance->objects[user_id].x;
	lua_pushnumber(L, x);			// ������ ��
	return 1;
}

int GameFramework::API_get_y(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	int y = instance->objects[user_id].y;
	lua_pushnumber(L, y);
	return 1;
}

int GameFramework::API_SendMessage(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);		// npc
	int user_id = (int)lua_tointeger(L, -2);	// player
	char* mess = (char*)lua_tostring(L, -1);

	lua_pop(L, 4);

	instance->objects[user_id].send_chat_packet(instance->objects[my_id], mess);
	return 0;
}

int GameFramework::API_RunAway(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);		// npc
	int user_id = (int)lua_tointeger(L, -2);	// player
	char* mess = (char*)lua_tostring(L, -1);

	lua_pop(L, 4);

	TIMER_EVENT ev{ my_id, std::chrono::system_clock::now(), TIMER_EVENT::TE_RANDOM_MOVE, user_id };
	instance->timer_queue.push(ev);
	ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::seconds{ 1 };
	instance->timer_queue.push(ev);
	ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::seconds{ 2 };
	instance->timer_queue.push(ev);
	ev.wakeup_time = std::chrono::system_clock::now() + std::chrono::seconds{ 3 };
	ev.event_id = TIMER_EVENT::TE_AI_BYE;
	instance->timer_queue.push(ev);

	strcpy_s(instance->objects[my_id].ai_msg, mess);

	//clients[user_id].send_chat_packet(my_id, mess);
	return 0;
}

void GameFramework::setStaticInstance(GameFramework& instance)
{
	GameFramework::instance = &instance;
}

GameFramework::GameFramework(const HANDLE& h_iocp,
	concurrency::concurrent_priority_queue<TIMER_EVENT>& timer_queue,
	concurrency::concurrent_queue<std::shared_ptr<DB_EVENT>>& db_queue)
	: iocp_handle{ h_iocp }, timer_queue{ timer_queue }, db_queue{ db_queue }
{
}

void GameFramework::initializeNPC()
{
	std::cout << "NPC intialize begin.\n";
	for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
		objects[i].x = rand() % W_WIDTH;
		objects[i].y = rand() % W_HEIGHT;

		// NPC�� ����, �̵��� �� ���Ͱ� �������� �Ѵ�.
		// ���� ��ġ ����
		{
			auto s_idx = SECTOR::getSectorIndex(objects[i].x, objects[i].y);
			objects[i].sec_idx = s_idx;
			sectors[s_idx.first][s_idx.second].object_list.insert(i);
		}


		objects[i].id = i;

		objects[i].level = rand() % 10 + 1;				// 1 ~ 10
		objects[i].max_hp = objects[i].level * 100;		// max_hp�� level * 100
		objects[i].hp = objects[i].max_hp;				// �ʱ�hp�� max�� ����
		objects[i].exp = 0;								// NPC�� ����ġ�� �ǹ̾���.

		if (i < MAX_USER + MAX_AGRO)
			sprintf_s(objects[i].name, "AGRO%d", i);
		else
			sprintf_s(objects[i].name, "PEACE%d", i);

		objects[i].state = Session::ST_INGAME;

		auto L = objects[i].s_Lua = luaL_newstate();
		luaL_openlibs(L);
		luaL_loadfile(L, "npc.lua");
		lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, i);
		lua_pcall(L, 1, 0, 0);
		// lua_pop(L, 1);// eliminate set_uid from stack after call

		lua_register(L, "API_RunAway", API_RunAway);
		lua_register(L, "API_SendMessage", API_SendMessage);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
	}
	std::cout << "NPC initialize end.\n";
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
	while (remain_data > 1) {
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

		for (int t : objects[c_id].be_targeted) {
			objects[t].target_obj = -1;
		}

		closesocket(objects[c_id].socket);

		// DB�� ����
		auto ptr = std::make_shared<DB_EVENT_SAVE>(c_id, objects[c_id].name, objects[c_id].x, objects[c_id].y, objects[c_id].level, objects[c_id].max_hp, objects[c_id].hp, objects[c_id].exp);
		db_queue.push(ptr);		// DBť�� �־ �̸��� Ȯ���� �ش�...
	}

	std::lock_guard<std::mutex> ll(objects[c_id].socket_lock);
	objects[c_id].state = Session::ST_FREE;
}

void GameFramework::tryNpcMove(int npc_id)
{
	bool keep_alive = false;
	for (int j = 0; j < MAX_USER; ++j) {
		if (objects[j].state != Session::ST_INGAME) continue;
		if (can_see(npc_id, j)) {
			keep_alive = true;
			//doNpcRandomMove(npc_id);		// ���⼭ �ٷ� ���ش� (�ѹ��� ����)
			break;
		}
	}
	if (true == keep_alive) {
		doNpcRandomMove(npc_id);
		TIMER_EVENT ev{ npc_id, std::chrono::system_clock::now() + std::chrono::seconds(1) , TIMER_EVENT::TE_RANDOM_MOVE, 0 };
		timer_queue.push(ev);
		// ���⼭ �÷��̾� ���� �߰�
		if (objects[npc_id].target_obj >= 0) {
			int target_obj = objects[npc_id].target_obj;
			short x = objects[target_obj].x;
			short y = objects[target_obj].y;

			if ((x + 1 == objects[npc_id].x and y == objects[npc_id].y)
				or (x - 1 == objects[npc_id].x and y == objects[npc_id].y)
				or (x == objects[npc_id].x and y + 1 == objects[npc_id].y)
				or (x == objects[npc_id].x and y - 1 == objects[npc_id].y)
				) {		// �� Ÿ���� ���� ���� ������
				// --------------------------------------------------------------------------------------
				objects[target_obj].hp -= objects[npc_id].level * 5;					// ���� level * 5 �� ��� �Ѵ�

				if (objects[target_obj].hp <= 0) {									// �÷��̾ ������
					for (int a : objects[target_obj].be_targeted)
						if (objects[a].target_obj == target_obj)
							objects[a].target_obj = -1;								// ���� Ÿ������ �ϴ� ģ���� �ʱ�ȭ
					objects[target_obj].be_targeted.clear();

					objects[target_obj].hp = objects[target_obj].max_hp;			// ü�� �ʱ�ȭ
					if (objects[target_obj].exp != 0)
						objects[target_obj].exp /= 2;								// ����ġ ���
					objects[target_obj].x = objects[target_obj].start_x;
					objects[target_obj].y = objects[target_obj].start_y;			// ������ġ��

					char mess[20];
					sprintf_s(mess, "You Died zz");
					objects[target_obj].send_chat_packet(objects[target_obj], mess);
					objects[target_obj].send_move_packet(objects[target_obj]);				// ������ġ�� �ٲ�� ������ �̵���Ŷ
				}
				else {
					char mess[10];
					sprintf_s(mess, "-%d HP", objects[npc_id].level * 5);
					objects[target_obj].send_chat_packet(objects[target_obj], mess);
				}

				objects[target_obj].send_stat_change_packet();						// �÷��̾��� ����� ������ ����
				std::cout << "player[" << target_obj << "] - hp : " << objects[target_obj].hp << std::endl;
				// --------------------------------------------------------------------------------------
			}

		}
	}
	else {
		objects[npc_id].is_active = false;
	}
}

void GameFramework::npcBye(int to, int from)
{
	objects[to].send_chat_packet(objects[from], objects[from].ai_msg);
}

void GameFramework::callbackPlayerMove(int npc_id, int p_id)
{
	objects[npc_id].lua_lock.lock();
	auto L = objects[npc_id].s_Lua;
	lua_getglobal(L, "event_player_move");
	lua_pushnumber(L, p_id);
	lua_pcall(L, 1, 0, 0);
	//lua_pop(L, 1);	// ���ϰ��� ���� ȣ���̱⿡ pop���� �ʴ´�.
	objects[npc_id].lua_lock.unlock();
}

void GameFramework::callbackDBLogin(int c_id, EXP_EXP_OVER* ex)
{
	if (ex->exp_over.rw_buf[0]) {	// ������ �α����� 0���ε����� 0 �־��
		strcpy_s(objects[c_id].name, ex->exp_over.rw_buf);
		{
			std::lock_guard<std::mutex> ll{ objects[c_id].socket_lock };
			objects[c_id].x = objects[c_id].start_x = ex->x;		// ���⿡ �־����.
			objects[c_id].y = objects[c_id].start_y = ex->y;
			objects[c_id].level = ex->level;
			objects[c_id].max_hp = ex->max_hp;
			objects[c_id].hp = ex->hp;
			objects[c_id].exp = ex->exp;
			objects[c_id].state = Session::ST_INGAME;
		}

		// ���� ��ġ ����
		auto s_idx = SECTOR::getSectorIndex(objects[c_id].x, objects[c_id].y);
		objects[c_id].sec_idx = s_idx;
		sectors[s_idx.first][s_idx.second].sector_m.lock();
		sectors[s_idx.first][s_idx.second].object_list.insert(c_id);
		sectors[s_idx.first][s_idx.second].sector_m.unlock();

		objects[c_id].send_login_info_packet();
		for (auto& pl : objects) {
			{
				std::lock_guard<std::mutex> ll(pl.socket_lock);
				if (Session::ST_INGAME != pl.state) continue;
			}
			if (pl.id == c_id) continue;
			if (false == can_see(c_id, pl.id))
				continue;
			if (is_pc(pl.id)) pl.send_add_player_packet(objects[c_id]);
			else wakeUpNPC(pl.id, c_id);		// ��ó�� npc�� �����
			objects[c_id].send_add_player_packet(objects[pl.id]);
		}

		// �¿���� 1���;� �� ����, �� ��ó�� �� 9�� ���͸� ���ؼ� �þ� ���� �ִ� �÷��̾�� add��Ŷ ����
		// ���� ���� ������, ST_INGAME���� �Ǵ�
		for (int i = s_idx.first - 1; i <= s_idx.first + 1; ++i) {
			if (i < 0 or i > W_WIDTH / SECTOR_RANGE - 1)	// ������ ��� ���� Ž������ �ʴ´�
				continue;
			for (int j = s_idx.second - 1; j <= s_idx.second + 1; ++j) {
				if (j < 0 or j > W_HEIGHT / SECTOR_RANGE - 1)
					continue;
				sectors[i][j].sector_m.lock();
				std::unordered_set<int> sec_lists = sectors[i][j].object_list;
				sectors[i][j].sector_m.unlock();

				for (auto p_id : sec_lists) {
					{
						std::lock_guard<std::mutex> ll(objects[p_id].socket_lock);
						if (Session::ST_INGAME != objects[p_id].state) continue;
					}
					if (false == can_see(p_id, c_id)) continue;
					if (p_id == c_id) continue;
					if (is_pc(p_id)) objects[p_id].send_add_player_packet(objects[c_id]);
					else wakeUpNPC(p_id, c_id);		// ��ó�� npc�� �����
					objects[c_id].send_add_player_packet(objects[p_id]);
				}
			}
		}

		TIMER_EVENT ev{ c_id, std::chrono::system_clock::now() + std::chrono::seconds(5), TIMER_EVENT::TE_HP_CHARGE, 0 };
		timer_queue.push(ev);
	}
	else {		// �α��� ����
		//cout << c_id << ": disconnect" << endl;
		objects[c_id].send_login_fail_packet();	// fail ��Ŷ ������
		// Ŭ��� �������� �������� ����, fail packet�� ������
	}
}

void GameFramework::doRespawn(int c_id)
{
	objects[c_id].isDead = false;
	for (int j = 0; j < MAX_USER; ++j) {
		if (objects[j].state != Session::ST_INGAME) continue;
		if (can_see(c_id, j)) {
			objects[j].send_add_player_packet(objects[c_id]);
			break;
		}
	}
}

void GameFramework::doHpCharge(int c_id)
{
	if (objects[c_id].state == Session::ST_INGAME) {			// 5�ʸ��� ü������
		objects[c_id].hp = min(objects[c_id].max_hp, int(std::round(objects[c_id].hp + objects[c_id].max_hp * 0.1)));		// �ִ� ü���� 10����
		objects[c_id].send_stat_change_packet();
		TIMER_EVENT ev{ c_id, std::chrono::system_clock::now() + std::chrono::seconds(5), TIMER_EVENT::TE_HP_CHARGE, 0 };
		timer_queue.push(ev);
	}
}

void GameFramework::processPacket(int c_id, char* packet)
{
	switch (packet[2]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);

		auto ptr = std::make_shared<DB_EVENT_LOGIN>(c_id, p->name);
		db_queue.push(ptr);		// DBť�� �־ �̸��� Ȯ���� �ش�...

		break;
	}
	case CS_CHAT: {
		CS_CHAT_PACKET* p = reinterpret_cast<CS_CHAT_PACKET*>(packet);

		objects[c_id].view_lock.lock();
		std::unordered_set<int> near_list = objects[c_id].view_list;
		objects[c_id].view_lock.unlock();

		SC_CHAT_PACKET packet;
		packet.type = SC_CHAT;
		packet.size = sizeof(packet);
		packet.id = c_id;
		strcpy_s(packet.mess, p->mess);

		objects[c_id].doSend(&packet);
		for (auto& pl : near_list) {
			auto& cpl = objects[pl];
			if (is_pc(pl)) {
				objects[pl].doSend(&packet);
			}
		}

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
		case 0: if (y > 0 and isMoveAble(x, y - 1)) y--; break;
		case 1: if (y < W_HEIGHT - 1 and isMoveAble(x, y + 1)) y++; break;
		case 2: if (x > 0 and isMoveAble(x - 1, y)) x--; break;
		case 3: if (x < W_WIDTH - 1 and isMoveAble(x + 1, y)) x++; break;
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
					if (objects[p_id].isDead) continue;		//-------------------------------------- �׾����� �þ߿��� �������.
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
	case CS_ATTACK: {
		//std::cout << "[" << c_id << "] - Attack_Packet" << std::endl;
// CS_ATTACK ��Ŷ�� ������ ����� ��� �÷��̾�		// �÷��̾�� NPC�� ����
		for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
			if (objects[i].state != Session::ST_INGAME) continue;
			if (objects[i].isDead) continue;			// �̹� �׾����� �ǵ帮�� �ʴ´�

			short x = objects[i].x;
			short y = objects[i].y;

			if ((x + 1 == objects[c_id].x and y == objects[c_id].y)
				or (x - 1 == objects[c_id].x and y == objects[c_id].y)
				or (x == objects[c_id].x and y + 1 == objects[c_id].y)
				or (x == objects[c_id].x and y - 1 == objects[c_id].y)
				) {														// �÷��̾��� ���ݹ����� ���� �����ִ� npc�� �����Ѵٸ�

				objects[i].hp -= objects[c_id].level * 30;					// ���� level * 30 �� ��� �Ѵ�
				objects[i].target_obj = c_id;								// ���ݹ����� Ÿ���� �����ȴ�.
				objects[c_id].be_targeted.insert(i);						// ���� Ÿ���� �� NPC�� ����صд�.	// �ߺ�����

				if (objects[i].hp <= 0) {								// ������ dead ���ְ�, Ÿ�̸ӿ� ���� - 30���� ��Ȱ
					objects[i].isDead = true;
					objects[i].target_obj = -1;							// ������ ���̻� Ÿ������ ���� �ʴ´�.
					objects[i].hp = objects[i].max_hp;					// ��Ȱ�� ü�� �ʱ�ȭ�� �̸� ���ش�.

					objects[c_id].send_remove_player_packet(i);			// �ٷ� ������ ����� �þ߿��� ����

					auto lev = objects[i].level;
					if (is_Agro_npc(i))
						objects[c_id].exp += lev * lev * 30;				// ����ġ ���	// Agro Peace �ٸ���
					else
						objects[c_id].exp += lev * lev * 15;
					while (true) {
						int need_exp = int(pow(2, objects[c_id].level - 1) * 100);
						if (objects[c_id].exp >= need_exp) {
							objects[c_id].exp -= need_exp;
							++objects[c_id].level;
							objects[c_id].max_hp += 100;
							objects[c_id].hp = min(objects[c_id].max_hp, objects[c_id].hp + 100);
						}
						else
							break;
					}
					objects[c_id].send_stat_change_packet();		// �ٲ� ����ġ, level ����

					TIMER_EVENT ev{ i, std::chrono::system_clock::now() + std::chrono::seconds(30), TIMER_EVENT::TE_RESPAWN, 0 };
					timer_queue.push(ev);
				}
				else {
					char mess[10];				// ���ݹ����� ������ ������ ���� ü���� ������ �÷��̾�� ǥ��
					sprintf_s(mess, "%d:%d", objects[i].level, objects[i].hp);
					objects[c_id].send_chat_packet(objects[i], mess);
				}
				objects[i].send_stat_change_packet();
				std::cout << "objects[" << i << "] - hp : " << objects[i].hp << std::endl;
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

	if (objects[npc_id].is_active) return;
	bool old_state = false;
	if (false == std::atomic_compare_exchange_strong(&objects[npc_id].is_active, &old_state, true))
		return;
	TIMER_EVENT ev{ npc_id, std::chrono::system_clock::now(), TIMER_EVENT::TE_RANDOM_MOVE, 0 };
	timer_queue.push(ev);
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

	if (is_Agro_npc(npc_id) and not npc.isDead) {		// Agro NPC�� RandomMove ����
		int x = npc.x;
		int y = npc.y;

		if (objects[npc_id].target_obj >= 0) {
			int dx = objects[objects[npc_id].target_obj].x - objects[npc_id].x;
			int dy = objects[objects[npc_id].target_obj].y - objects[npc_id].y;

			if (std::abs(dx) >= std::abs(dy)) {
				if (dx > 0)
					++x;
				else
					--x;
			}
			else {
				if (dy > 0)
					++y;
				else
					--y;
			}
			if (not isMoveAble(x, y)) {
				x = npc.x;
				y = npc.y;
				switch (rand() % 4) {
				case 0: if (x < (W_WIDTH - 1) and isMoveAble(x + 1, y)) x++; break;
				case 1: if (x > 0 and isMoveAble(x - 1, y)) x--; break;
				case 2: if (y < (W_HEIGHT - 1) and isMoveAble(x, y + 1)) y++; break;
				case 3:if (y > 0 and isMoveAble(x, y - 1)) y--; break;
				}
			}
		}
		else {
			switch (rand() % 4) {
			case 0: if (x < (W_WIDTH - 1) and isMoveAble(x + 1, y)) x++; break;
			case 1: if (x > 0 and isMoveAble(x - 1, y)) x--; break;
			case 2: if (y < (W_HEIGHT - 1) and isMoveAble(x, y + 1)) y++; break;
			case 3:if (y > 0 and isMoveAble(x, y - 1)) y--; break;
			}
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

bool GameFramework::is_Agro_npc(int object_id)
{
	return !is_pc(object_id) and object_id < MAX_USER + MAX_AGRO;
}

bool GameFramework::is_Peace_npc(int object_id)
{
	return !is_pc(object_id) and !is_Agro_npc(object_id);
}
