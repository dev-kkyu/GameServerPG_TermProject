#include "EXP_OVERLAPPED.h"

EXP_OVERLAPPED::EXP_OVERLAPPED(COMP_TYPE c_type, unsigned buf_len)
	: comp_type{ c_type }, wsaover{}, rw_buf{}
{
	wsabuf.len = buf_len;
	wsabuf.buf = rw_buf;
}
