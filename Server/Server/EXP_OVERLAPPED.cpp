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
