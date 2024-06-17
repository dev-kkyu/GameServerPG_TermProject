#include "NetworkManager.h"

#include <WS2tcpip.h>
#include <MSWSock.h>	// AcceptEX

// ��ũ ���̺귯��
#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")

#include <iostream>

NetworkManager::NetworkManager(unsigned short port)
	: gameFramework{ handle_iocp }
{
	::WSADATA WSAData;
	::WSAStartup(MAKEWORD(2, 2), &WSAData);
	server_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
	::SOCKADDR_IN server_addr{};	// �ʱ�ȭ
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	::bind(server_socket, reinterpret_cast<SOCKADDR*>(&server_addr), sizeof(server_addr));
	::listen(server_socket, SOMAXCONN);

	// Todo :  NPC �ʱ�ȭ �Լ� ȣ��

	handle_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);	// ������ ���� 0�̸� hardware_concurrency ��
	::CreateIoCompletionPort(reinterpret_cast<HANDLE>(server_socket), handle_iocp, 99999, 0);		// server socket�� accept �Ϸ� ������ ���Ͽ� ��� (Ű�� �Ⱦ��� Ű)
	accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);			// AcceptEX ����� ���� Accept �� ������ ���� �����
	int addr_size = sizeof(SOCKADDR_IN);									// IPv4 �ּ� ���� ����ü�� ũ��
	::AcceptEx(server_socket, accept_socket, accept_over.rw_buf, 0, addr_size + 16, addr_size + 16, nullptr, &accept_over.wsaover);

}

NetworkManager::~NetworkManager()
{
	::closesocket(server_socket);
	::WSACleanup();
}

void NetworkManager::run()
{
	while (true) {
		DWORD num_bytes;
		ULONG_PTR key;
		::WSAOVERLAPPED* wsaover = nullptr;
		BOOL ret = ::GetQueuedCompletionStatus(handle_iocp, &num_bytes, &key, &wsaover, INFINITE);
		EXP_OVERLAPPED* exp_over = reinterpret_cast<EXP_OVERLAPPED*>(wsaover);		// Ȯ�� �������� Ŭ������ �ٲ��ش� (IOCP�� ��ϵ� WSAOVER�� �� Ȯ��� WSAOVER�̴�)

		// GQCS ���н�
		if (FALSE == ret) {
			if (exp_over->comp_type == EXP_OVERLAPPED::OP_ACCEPT)
				std::cout << "Accept Error";
			else {
				std::cout << "GQCS Error on client[" << key << "]\n";
				gameFramework.disconnect(static_cast<int>(key));
				if (exp_over->comp_type == EXP_OVERLAPPED::OP_SEND)
					delete exp_over;
				continue;
			}
		}

		// Ŭ���̾�Ʈ�� ����
		if ((0 == num_bytes) && ((exp_over->comp_type == EXP_OVERLAPPED::OP_RECV) || (exp_over->comp_type == EXP_OVERLAPPED::OP_SEND))) {
			gameFramework.disconnect(static_cast<int>(key));
			if (exp_over->comp_type == EXP_OVERLAPPED::OP_SEND)
				delete exp_over;
			continue;
		}

		switch (exp_over->comp_type)
		{
		case EXP_OVERLAPPED::OP_ACCEPT: {
			int client_id = gameFramework.getNewClientID();
			if (client_id != -1) {
				::CreateIoCompletionPort(reinterpret_cast<HANDLE>(accept_socket),
					handle_iocp, client_id, 0);	// Accept �� ���� IOCP �ڵ鿡 ������ֱ�
				gameFramework.clientStart(client_id, accept_socket);

				accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);	// �� Accept�� ���� ����� �ֱ�
			}
			else {
				std::cout << "Max user exceeded.\n";
			}
			::memset(&accept_over.wsaover, 0, sizeof(accept_over.wsaover));
			int addr_size = sizeof(SOCKADDR_IN);
			::AcceptEx(server_socket, accept_socket, accept_over.rw_buf,		// ���ο� ������ �޴´�
				0, addr_size + 16, addr_size + 16, nullptr, &accept_over.wsaover);
			break;
		}
		case EXP_OVERLAPPED::OP_RECV: {
			gameFramework.processRecv(static_cast<int>(key), num_bytes);
			break;
		}
		case EXP_OVERLAPPED::OP_SEND: {
			delete exp_over;	// ���� �Ϸ�� delete�� ���ش�
			break;
		}
		}	// switch (exp_over->comp_type)

	}
}
