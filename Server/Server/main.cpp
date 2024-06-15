#include <iostream>
#include <vector>
#include <thread>

#include <WS2tcpip.h>
#include <MSWSock.h>	// AcceptEX

#include "protocol_2024.h"

#include "GameFramework.h"

// ��ũ ���̺귯��
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
		EXP_OVERLAPPED* exp_over = reinterpret_cast<EXP_OVERLAPPED*>(wsaover);		// Ȯ�� �������� Ŭ������ �ٲ��ش� (IOCP�� ��ϵ� WSAOVER�� �� Ȯ��� WSAOVER�̴�)

		// GQCS ���н�
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

		// Ŭ���̾�Ʈ�� ����
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
					g_handle_iocp, client_id, 0);	// Accept �� ���� IOCP �ڵ鿡 ������ֱ�
				g_GameFramework.clientStart(client_id, g_accept_socket);

				g_accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);	// �� Accept�� ���� ����� �ֱ�
			}
			else {
				std::cout << "Max user exceeded.\n";
			}
			::memset(&g_accept_over.wsaover, 0, sizeof(g_accept_over.wsaover));
			int addr_size = sizeof(SOCKADDR_IN);
			::AcceptEx(g_server_socket, g_accept_socket, g_accept_over.rw_buf,		// ���ο� ������ �޴´�
				0, addr_size + 16, addr_size + 16, nullptr, &g_accept_over.wsaover);
			break;
		}
		case EXP_OVERLAPPED::OP_RECV: {
			g_GameFramework.processRecv(static_cast<int>(key), num_bytes);
			break;
		}
		case EXP_OVERLAPPED::OP_SEND: {
			delete exp_over;	// ���� �Ϸ�� delete�� ���ش�
			break;
		}
		}	// switch (exp_over->comp_type)

	}
}

int main()
{
	::setlocale(LC_ALL, "korean");		// DB ������� �ѱ� ���ؼ�. ��Ʈ��ũ ���� ��¿��� �ʿ�

	::WSADATA WSAData;
	::WSAStartup(MAKEWORD(2, 2), &WSAData);
	g_server_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
	::SOCKADDR_IN server_addr{};	// �ʱ�ȭ
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	::bind(g_server_socket, reinterpret_cast<SOCKADDR*>(&server_addr), sizeof(server_addr));
	::listen(g_server_socket, SOMAXCONN);

	// Todo :  NPC �ʱ�ȭ �Լ� ȣ��

	g_handle_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);	// ������ ���� 0�̸� hardware_concurrency ��
	::CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_server_socket), g_handle_iocp, 99999, 0);		// server socket�� accept �Ϸ� ������ ���Ͽ� ��� (Ű�� �Ⱦ��� Ű)
	g_accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);			// AcceptEX ����� ���� Accept �� ������ ���� �����
	int addr_size = sizeof(SOCKADDR_IN);									// IPv4 �ּ� ���� ����ü�� ũ��
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
