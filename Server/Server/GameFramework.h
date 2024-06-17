#pragma once

#include <array>

#include "Session.h"

class SECTOR
{
public:
	std::unordered_set<int> object_list;
	std::mutex sector_m;

	static std::pair<int, int> getSectorIndex(int pos_x, int pos_y);
};

class GameFramework
{
private:
	friend class SECTOR;
	static constexpr int VIEW_RANGE = 5;
	static constexpr int SECTOR_RANGE = 20;

	static GameFramework* instance;

private:
	static int API_get_x(lua_State* L);
	static int API_get_y(lua_State* L);
	static int API_SendMessage(lua_State* L);
	static int API_RunAway(lua_State* L);

private:
	const HANDLE& iocp_handle;

	std::array<Session, MAX_USER + MAX_NPC> objects;
	SECTOR sectors[W_WIDTH / SECTOR_RANGE][W_HEIGHT / SECTOR_RANGE];

public:
	static void setStaticInstance(GameFramework& instance);

	GameFramework(const HANDLE& h_iocp);

	void initializeNPC();

	int getNewClientID();
	void clientStart(int c_id, ::SOCKET c_socket);
	void processRecv(int c_id, int recv_size);
	void disconnect(int c_id);

	void tryNpcMove(int npc_id);

private:
	void processPacket(int c_id, char* packet);
	void wakeUpNPC(int npc_id, int waker);

	void doNpcRandomMove(int npc_id);

private:	// ÇïÆÛ ÇÔ¼öµé
	bool is_pc(int object_id);
	bool is_npc(int object_id);
	bool can_see(int from, int to);

};

