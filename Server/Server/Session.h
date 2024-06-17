#pragma once

#include <utility>
#include <mutex>
#include <atomic>
#include <unordered_set>

#include "EXP_OVERLAPPED.h"		// 확장 WSAOVERLAPPED 클래스

struct lua_State;
class Session
{
public:
	enum STATE { ST_FREE, ST_ALLOC, ST_INGAME };

public:
	EXP_OVERLAPPED recv_over;

	::SOCKET socket;
	std::mutex socket_lock;

	STATE state;

	int id;
	short x, y;
	char	name[NAME_SIZE];

	int		prev_remain;

	int		last_move_time;

	std::atomic_bool is_active;	// 주위에 플레이어가 있는가?

	std::unordered_set<int> view_list;
	std::mutex view_lock;

	lua_State* s_Lua;
	std::mutex lua_lock;
	char ai_msg[CHAT_SIZE];

	std::pair<short, short> sec_idx;		// 섹터 인덱스(x, y)

	// 모든 오브젝트는 체력, 경험치, 레벨이 존재
	int		hp;
	int		max_hp;
	int		exp;
	int		level;

	// 죽었는지 체크하는 변수 추가
	bool isDead;

	// NPC : 공격받으면 역공 해야함
	int target_obj;

	// 시작좌표 저장
	short start_x, start_y;

	// 날 타겟으로 한 NPC
	std::unordered_set<int> be_targeted;

public:
	Session();

	void doRecv();
	void doSend(void* packet);

	void send_login_info_packet();

	void send_stat_change_packet();

	void send_login_fail_packet();
	void send_move_packet(const Session& other);
	void send_add_player_packet(const Session& other);
	void send_chat_packet(const Session& other, const char* mess);
	void send_remove_player_packet(int c_id);

};

