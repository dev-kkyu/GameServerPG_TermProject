#include "EVENTS.h"

#include <string>

DB_EVENT::DB_EVENT(int obj_id)
	: obj_id{ obj_id }
{
}

DB_EVENT::~DB_EVENT()
{
}

DB_EVENT_LOGIN::DB_EVENT_LOGIN(int obj_id, const char* name)
	: DB_EVENT{ obj_id }
{
	event_id = DE_LOGIN;
	strcpy_s(login_id, name);
}

DB_EVENT_LOGIN::~DB_EVENT_LOGIN()
{
}

DB_EVENT_SAVE::DB_EVENT_SAVE(int obj_id, const char* name, short x, short y)
	: DB_EVENT{ obj_id }
{
	event_id = DE_SAVE;
	strcpy_s(login_id, name);
	pos_x = x;
	pos_y = y;
}

DB_EVENT_SAVE::~DB_EVENT_SAVE()
{
}
