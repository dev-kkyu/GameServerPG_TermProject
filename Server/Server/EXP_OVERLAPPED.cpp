#include "EXP_OVERLAPPED.h"

EXP_OVERLAPPED::EXP_OVERLAPPED(COMP_TYPE c_type)
	: comp_type{ c_type }, wsaover{}, rw_buf{}
{
	wsabuf.len = BUF_SIZE;
	wsabuf.buf = rw_buf;
}

EXP_OVERLAPPED::EXP_OVERLAPPED(void* packet)
	: comp_type{ OP_SEND }, wsaover{}
{
	unsigned short buf_len = reinterpret_cast<unsigned short*>(packet)[0];
	wsabuf.len = buf_len;
	wsabuf.buf = rw_buf;
	memcpy(rw_buf, packet, buf_len);
}

EXP_EXP_OVER::EXP_EXP_OVER(const char* name, short x, short y, short level, int max_hp, int hp, int exp)
	: exp_over{ EXP_OVERLAPPED::OP_DB_LOGIN }, x{ x }, y{ y }, level{ level }, max_hp{ max_hp }, hp{ hp }, exp{ exp }
{
	strcpy_s(exp_over.rw_buf, name);
}
