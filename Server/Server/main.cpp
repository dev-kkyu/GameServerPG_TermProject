#include <iostream>
#include <vector>
#include <thread>

#include <WS2tcpip.h>
#include <MSWSock.h>	// AcceptEX

#include "protocol_2024.h"

#include "GameFramework.h"

// 링크 라이브러리
#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")

static ::SOCKET g_server_socket;
static ::SOCKET g_accept_socket;
static ::EXP_OVERLAPPED g_accept_over{ EXP_OVERLAPPED::OP_ACCEPT };
static ::HANDLE g_handle_iocp;

static GameFramework g_GameFramework;

void worker_thread()
{
	while (true) {
		DWORD num_bytes;
		ULONG_PTR key;
		::WSAOVERLAPPED* wsaover = nullptr;
		BOOL ret = ::GetQueuedCompletionStatus(g_handle_iocp, &num_bytes, &key, &wsaover, INFINITE);
		EXP_OVERLAPPED* exp_over = reinterpret_cast<EXP_OVERLAPPED*>(wsaover);		// 확장 오버랩드 클래스로 바꿔준다 (IOCP에 등록된 WSAOVER는 다 확장된 WSAOVER이다)

		// GQCS 실패시
		if (FALSE == ret) {
			if (exp_over->comp_type == EXP_OVERLAPPED::OP_ACCEPT)
				std::cout << "Accept Error";
			else {
				std::cout << "GQCS Error on client[" << key << "]\n";
				g_GameFramework.disconnect(static_cast<int>(key));
				if (exp_over->comp_type == EXP_OVERLAPPED::OP_SEND)
					delete exp_over;
				continue;
			}
		}

		// 클라이언트의 종료
		if ((0 == num_bytes) && ((exp_over->comp_type == EXP_OVERLAPPED::OP_RECV) || (exp_over->comp_type == EXP_OVERLAPPED::OP_SEND))) {
			g_GameFramework.disconnect(static_cast<int>(key));
			if (exp_over->comp_type == EXP_OVERLAPPED::OP_SEND)
				delete exp_over;
			continue;
		}

		switch (exp_over->comp_type)
		{
		case EXP_OVERLAPPED::OP_ACCEPT: {
			int client_id = g_GameFramework.getNewClientID();
			if (client_id != -1) {
				::CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_accept_socket),
					g_handle_iocp, client_id, 0);	// Accept 된 소켓 IOCP 핸들에 등록해주기
				g_GameFramework.clientStart(client_id, g_accept_socket);

				g_accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);	// 새 Accept용 소켓 만들어 주기
			}
			else {
				std::cout << "Max user exceeded.\n";
			}
			::memset(&g_accept_over.wsaover, 0, sizeof(g_accept_over.wsaover));
			int addr_size = sizeof(SOCKADDR_IN);
			::AcceptEx(g_server_socket, g_accept_socket, g_accept_over.rw_buf,		// 새로운 접속을 받는다
				0, addr_size + 16, addr_size + 16, nullptr, &g_accept_over.wsaover);
			break;
		}
		case EXP_OVERLAPPED::OP_RECV: {
			g_GameFramework.processRecv(static_cast<int>(key), num_bytes);
			break;
		}
		case EXP_OVERLAPPED::OP_SEND: {
			delete exp_over;	// 전송 완료시 delete를 해준다
			break;
		}
		}	// switch (exp_over->comp_type)

	}
}

int main()
{
	::setlocale(LC_ALL, "korean");		// DB 오류출력 한글 위해서. 네트워크 오류 출력에도 필요

	::WSADATA WSAData;
	::WSAStartup(MAKEWORD(2, 2), &WSAData);
	g_server_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
	::SOCKADDR_IN server_addr{};	// 초기화
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	::bind(g_server_socket, reinterpret_cast<SOCKADDR*>(&server_addr), sizeof(server_addr));
	::listen(g_server_socket, SOMAXCONN);

	// Todo :  NPC 초기화 함수 호출

	g_handle_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);	// 마지막 인자 0이면 hardware_concurrency 값
	::CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_server_socket), g_handle_iocp, 99999, 0);		// server socket에 accept 완료 통지를 위하여 등록 (키는 안쓰는 키)
	g_accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);			// AcceptEX 사용을 위해 Accept 될 소켓을 먼저 만든다
	int addr_size = sizeof(SOCKADDR_IN);									// IPv4 주소 전용 구조체의 크기
	::AcceptEx(g_server_socket, g_accept_socket, g_accept_over.rw_buf, 0, addr_size + 16, addr_size + 16, nullptr, &g_accept_over.wsaover);

	std::vector<std::thread> worker_threads;
	int num_threads = std::thread::hardware_concurrency();
	worker_threads.reserve(num_threads);
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker_thread);

	for (auto& th : worker_threads)
		th.join();

	::closesocket(g_server_socket);
	::WSACleanup();
}
