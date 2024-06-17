#pragma once

#include <chrono>

#include "protocol_2024.h"

struct TIMER_EVENT
{
	enum TIMER_EVENT_TYPE { TE_RANDOM_MOVE, TE_AI_BYE };

	int obj_id;
	std::chrono::system_clock::time_point wakeup_time;
	TIMER_EVENT_TYPE event_id;
	int target_id;

public:
	constexpr bool operator < (const TIMER_EVENT& L) const
	{
		return (wakeup_time > L.wakeup_time);
	}
};

struct DB_EVENT
{
	enum DB_EVENT_TYPE { DE_LOGIN, DE_SAVE };

protected:		// 부모는 생성될 수 없다..
	DB_EVENT(int obj_id);
	virtual ~DB_EVENT();

public:
	DB_EVENT_TYPE event_id;
	int obj_id;

};

struct DB_EVENT_LOGIN : public DB_EVENT
{
	DB_EVENT_LOGIN(int obj_id, const char* name);
	virtual ~DB_EVENT_LOGIN();

public:
	char login_id[NAME_SIZE];

};

struct DB_EVENT_SAVE : public DB_EVENT
{
	DB_EVENT_SAVE(int obj_id, const char* name, short x, short y);
	virtual ~DB_EVENT_SAVE();

public:
	char login_id[NAME_SIZE];
	short pos_x;
	short pos_y;

};

