#include "NetworkManager.h"

#include <WS2tcpip.h>
#include <MSWSock.h>	// AcceptEX

// 링크 라이브러리
#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")

#include <iostream>

NetworkManager::NetworkManager(unsigned short port)
	: gameFramework{ handle_iocp }
{
	::WSADATA WSAData;
	::WSAStartup(MAKEWORD(2, 2), &WSAData);
	server_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
	::SOCKADDR_IN server_addr{};	// 초기화
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	::bind(server_socket, reinterpret_cast<SOCKADDR*>(&server_addr), sizeof(server_addr));
	::listen(server_socket, SOMAXCONN);

	// Todo :  NPC 초기화 함수 호출

	handle_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);	// 마지막 인자 0이면 hardware_concurrency 값
	::CreateIoCompletionPort(reinterpret_cast<HANDLE>(server_socket), handle_iocp, 99999, 0);		// server socket에 accept 완료 통지를 위하여 등록 (키는 안쓰는 키)
	accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);			// AcceptEX 사용을 위해 Accept 될 소켓을 먼저 만든다
	int addr_size = sizeof(SOCKADDR_IN);									// IPv4 주소 전용 구조체의 크기
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
		EXP_OVERLAPPED* exp_over = reinterpret_cast<EXP_OVERLAPPED*>(wsaover);		// 확장 오버랩드 클래스로 바꿔준다 (IOCP에 등록된 WSAOVER는 다 확장된 WSAOVER이다)

		// GQCS 실패시
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

		// 클라이언트의 종료
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
					handle_iocp, client_id, 0);	// Accept 된 소켓 IOCP 핸들에 등록해주기
				gameFramework.clientStart(client_id, accept_socket);

				accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);	// 새 Accept용 소켓 만들어 주기
			}
			else {
				std::cout << "Max user exceeded.\n";
			}
			::memset(&accept_over.wsaover, 0, sizeof(accept_over.wsaover));
			int addr_size = sizeof(SOCKADDR_IN);
			::AcceptEx(server_socket, accept_socket, accept_over.rw_buf,		// 새로운 접속을 받는다
				0, addr_size + 16, addr_size + 16, nullptr, &accept_over.wsaover);
			break;
		}
		case EXP_OVERLAPPED::OP_RECV: {
			gameFramework.processRecv(static_cast<int>(key), num_bytes);
			break;
		}
		case EXP_OVERLAPPED::OP_SEND: {
			delete exp_over;	// 전송 완료시 delete를 해준다
			break;
		}

									// 여기부터는 key가 NPC만 온다
		case EXP_OVERLAPPED::OP_NPC_MOVE: {			// 플레이어가 NPC를 깨우면 타이머를 통하여 동작, cs_move->wakeup->(최초)(EV_R)->타이머->이곳
			gameFramework.tryNpcMove(static_cast<int>(key));
			delete exp_over;
			break;
		}
		//case EXP_OVERLAPPED::OP_AI_BYE: {
		//	int user_id = ex_over->_ai_target_obj;
		//	clients[user_id].send_chat_packet(static_cast<int>(key), clients[key]._ai_msg);
		//	delete ex_over;
		//	break;
		//}
		//case EXP_OVERLAPPED::OP_PLAYER_MOVE: {		// 플레이어가 한번 이동할 때마다 모든 NPC에게 발동..	cs_move->wakeup..
		//	clients[key]._ll.lock();
		//	auto L = clients[key]._L;
		//	lua_getglobal(L, "event_player_move");
		//	lua_pushnumber(L, ex_over->_ai_target_obj);
		//	lua_pcall(L, 1, 0, 0);
		//	//lua_pop(L, 1);	// 리턴값이 없는 호출이기에 pop하지 않는다.
		//	clients[key]._ll.unlock();
		//	delete ex_over;
		//	break;
		//}
		//case EXP_OVERLAPPED::OP_DB_LOGIN: {
		//	int c_id = static_cast<int>(key);
		//	if (ex_over->_send_buf[0]) {	// 실패한 로그인은 0번인덱스에 0 넣어둠
		//		strcpy_s(clients[c_id]._name, ex_over->_send_buf);
		//		{
		//			lock_guard<mutex> ll{ clients[c_id]._s_lock };
		//			clients[c_id].x = LOWORD(ex_over->_ai_target_obj);		// 여기에 넣어줬다.
		//			clients[c_id].y = HIWORD(ex_over->_ai_target_obj);
		//			clients[c_id]._state = ST_INGAME;
		//		}

		//		// 섹터 위치 삽입
		//		auto s_idx = SECTOR::getSectorIndex(clients[c_id].x, clients[c_id].y);
		//		clients[c_id].sec_idx = s_idx;
		//		g_sectors[s_idx.first][s_idx.second].sector_m.lock();
		//		g_sectors[s_idx.first][s_idx.second].clients.insert(c_id);
		//		g_sectors[s_idx.first][s_idx.second].sector_m.unlock();

		//		clients[c_id].send_login_info_packet();
		//		//for (auto& pl : clients) {
		//		//	{
		//		//		lock_guard<mutex> ll(pl._s_lock);
		//		//		if (ST_INGAME != pl._state) continue;
		//		//	}
		//		//	if (pl._id == c_id) continue;
		//		//	if (false == can_see(c_id, pl._id))
		//		//		continue;
		//		//	if (is_pc(pl._id)) pl.send_add_player_packet(c_id);
		//		//	else WakeUpNPC(pl._id, c_id);		// 근처의 npc를 깨운다
		//		//	clients[c_id].send_add_player_packet(pl._id);
		//		//}

		//		// 좌우상하 1섹터씩 더 보고, 내 근처의 총 9개 섹터를 비교해서 시야 내에 있는 플레이어에게 add패킷 전송
		//		// 섹터 내에 있으면, ST_INGAME으로 판단
		//		for (int i = s_idx.first - 1; i <= s_idx.first + 1; ++i) {
		//			if (i < 0 or i > W_WIDTH / SECTOR_RANGE - 1)	// 범위를 벗어난 곳은 탐색하지 않는다
		//				continue;
		//			for (int j = s_idx.second - 1; j <= s_idx.second + 1; ++j) {
		//				if (j < 0 or j > W_HEIGHT / SECTOR_RANGE - 1)
		//					continue;
		//				g_sectors[i][j].sector_m.lock();
		//				unordered_set<int> sec_lists = g_sectors[i][j].clients;
		//				g_sectors[i][j].sector_m.unlock();

		//				for (auto p_id : sec_lists) {
		//					{
		//						lock_guard<mutex> ll(clients[p_id]._s_lock);
		//						if (ST_INGAME != clients[p_id]._state) continue;
		//					}
		//					if (false == can_see(p_id, c_id)) continue;
		//					if (p_id == c_id) continue;
		//					if (is_pc(p_id)) clients[p_id].send_add_player_packet(c_id);
		//					else WakeUpNPC(p_id, c_id);		// 근처의 npc를 깨운다
		//					clients[c_id].send_add_player_packet(p_id);
		//				}
		//			}
		//		}
		//	}
		//	else {		// 로그인 실패
		//		//cout << c_id << ": disconnect" << endl;
		//		clients[c_id].send_login_fail_packet();	// fail 패킷 보낸다
		//		// 클로즈를 서버에서 시켜주지 말고, fail packet만 보내자
		//	}
		//	delete ex_over;
		//	break;
		//}

		}	// switch (exp_over->comp_type)

	}
}
