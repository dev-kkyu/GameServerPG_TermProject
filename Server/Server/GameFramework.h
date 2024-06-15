#pragma once

#include <array>

#include "Session.h"

class GameFramework
{
private:
	std::array<Session, MAX_USER + MAX_NPC> objects;

public:
	void processRecv(int c_id, int recv_size);
	void disconnect(int c_id);

private:
	void processPacket(int c_id, char* packet);

};

