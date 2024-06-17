#include "NetworkManager.h"

#include <WS2tcpip.h>
#include <MSWSock.h>	// AcceptEX

// ��ũ ���̺귯��
#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")

#include <iostream>
#include <string>

void disp_error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
	if (RetCode == SQL_INVALID_HANDLE) {
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
		// Hide data truncated..	// ������ �߸��� ����
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

bool db_error_check(SQLRETURN retcode)
{
	if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO) {
		return true;	// error�� true
	}
	return false;
}

NetworkManager::NetworkManager(unsigned short port)
	: gameFramework{ handle_iocp }
{
	gameFramework.setStaticInstance(gameFramework);

	::WSADATA WSAData;
	::WSAStartup(MAKEWORD(2, 2), &WSAData);
	server_socket = WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
	::SOCKADDR_IN server_addr{};	// �ʱ�ȭ
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	::bind(server_socket, reinterpret_cast<SOCKADDR*>(&server_addr), sizeof(server_addr));
	::listen(server_socket, SOMAXCONN);

	gameFramework.initializeNPC();

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

void NetworkManager::runWorker()
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

									// ������ʹ� key�� NPC�� �´�
		case EXP_OVERLAPPED::OP_NPC_MOVE: {			// �÷��̾ NPC�� ����� Ÿ�̸Ӹ� ���Ͽ� ����, cs_move->wakeup->(����)(EV_R)->Ÿ�̸�->�̰�
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
										//case EXP_OVERLAPPED::OP_PLAYER_MOVE: {		// �÷��̾ �ѹ� �̵��� ������ ��� NPC���� �ߵ�..	cs_move->wakeup..
										//	clients[key]._ll.lock();
										//	auto L = clients[key]._L;
										//	lua_getglobal(L, "event_player_move");
										//	lua_pushnumber(L, ex_over->_ai_target_obj);
										//	lua_pcall(L, 1, 0, 0);
										//	//lua_pop(L, 1);	// ���ϰ��� ���� ȣ���̱⿡ pop���� �ʴ´�.
										//	clients[key]._ll.unlock();
										//	delete ex_over;
										//	break;
										//}
										//case EXP_OVERLAPPED::OP_DB_LOGIN: {
										//	int c_id = static_cast<int>(key);
										//	if (ex_over->_send_buf[0]) {	// ������ �α����� 0���ε����� 0 �־��
										//		strcpy_s(clients[c_id]._name, ex_over->_send_buf);
										//		{
										//			lock_guard<mutex> ll{ clients[c_id]._s_lock };
										//			clients[c_id].x = LOWORD(ex_over->_ai_target_obj);		// ���⿡ �־����.
										//			clients[c_id].y = HIWORD(ex_over->_ai_target_obj);
										//			clients[c_id]._state = ST_INGAME;
										//		}

										//		// ���� ��ġ ����
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
										//		//	else WakeUpNPC(pl._id, c_id);		// ��ó�� npc�� �����
										//		//	clients[c_id].send_add_player_packet(pl._id);
										//		//}

										//		// �¿���� 1���;� �� ����, �� ��ó�� �� 9�� ���͸� ���ؼ� �þ� ���� �ִ� �÷��̾�� add��Ŷ ����
										//		// ���� ���� ������, ST_INGAME���� �Ǵ�
										//		for (int i = s_idx.first - 1; i <= s_idx.first + 1; ++i) {
										//			if (i < 0 or i > W_WIDTH / SECTOR_RANGE - 1)	// ������ ��� ���� Ž������ �ʴ´�
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
										//					else WakeUpNPC(p_id, c_id);		// ��ó�� npc�� �����
										//					clients[c_id].send_add_player_packet(p_id);
										//				}
										//			}
										//		}
										//	}
										//	else {		// �α��� ����
										//		//cout << c_id << ": disconnect" << endl;
										//		clients[c_id].send_login_fail_packet();	// fail ��Ŷ ������
										//		// Ŭ��� �������� �������� ����, fail packet�� ������
										//	}
										//	delete ex_over;
										//	break;
										//}

		}	// switch (exp_over->comp_type)

	}
}

void NetworkManager::runTimer()
{
	while (true) {
		TIMER_EVENT ev;
		auto current_time = std::chrono::system_clock::now();
		if (true == timer_queue.try_pop(ev)) {
			if (ev.wakeup_time > current_time) {
				timer_queue.push(ev);		// ����ȭ �ʿ�
				// timer_queue�� �ٽ� ���� �ʰ� ó���ؾ� �Ѵ�.
				std::this_thread::sleep_for(std::chrono::milliseconds(1));  // ����ð��� ���� �ȵǾ����Ƿ� ��� ���
				continue;
			}
			switch (ev.event_id) {
			case TIMER_EVENT::TE_RANDOM_MOVE: {
				EXP_OVERLAPPED* ov = new EXP_OVERLAPPED{ EXP_OVERLAPPED::OP_NPC_MOVE };
				PostQueuedCompletionStatus(handle_iocp, 1, ev.obj_id, &ov->wsaover);
				break;
			}
			case TIMER_EVENT::TE_AI_BYE: {
				EXP_OVERLAPPED* ov = new EXP_OVERLAPPED{ EXP_OVERLAPPED::OP_AI_BYE };
				ov->ai_target_obj = ev.target_id;
				PostQueuedCompletionStatus(handle_iocp, 1, ev.obj_id, &ov->wsaover);
				break;
			}
			}
			continue;		// ��� ���� �۾� ������
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));   // timer_queue�� ��� ������ ��� ��ٷȴٰ� �ٽ� ����
	}
}

void NetworkManager::runDB()
{
	SQLRETURN retcode;

	// Allocate environment handle  
	SQLHENV henv;
	retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
	if (db_error_check(retcode)) {
		std::cout << "DB ENV �Ҵ� ����!" << std::endl;
		exit(-1);
	}
	// Set the ODBC version environment attribute
	retcode = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);
	if (db_error_check(retcode)) {
		std::cout << "DB ENV Set ����!" << std::endl;
		exit(-1);
	}
	// Allocate connection handle
	SQLHDBC hdbc;
	retcode = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
	if (db_error_check(retcode)) {
		std::cout << "DB DBC �Ҵ� ����!" << std::endl;
		exit(-1);
	}
	// Set login timeout to 5 seconds  
	SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, (SQLPOINTER)5, 0);
	// Connect to data source
	retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2020180051_GSHW", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);
	if (db_error_check(retcode)) {
		std::cout << "DB Connect ����!" << std::endl;
		disp_error(hdbc, SQL_HANDLE_DBC, retcode);
		exit(-1);
	}
	// Allocate statement handle  
	SQLHSTMT hstmt = 0;
	retcode = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
	if (db_error_check(retcode)) {
		std::cout << "DB STMT �Ҵ� ����!" << std::endl;
		exit(-1);
	}

	std::cout << "DB ���� ����!" << std::endl;

	// ----------------------���� ����-------------------------
	while (true) {
		std::shared_ptr<DB_EVENT> ev;
		if (true == db_queue.try_pop(ev)) {
			switch (ev->event_id)
			{
			case DB_EVENT::DE_LOGIN: {
				DB_EVENT_LOGIN* p = dynamic_cast<DB_EVENT_LOGIN*>(ev.get());

				EXP_OVERLAPPED* exp = new EXP_OVERLAPPED{ EXP_OVERLAPPED::OP_DB_LOGIN };

				std::string str = "SELECT user_pos_x, user_pos_y FROM user_table WHERE user_id = \'" + std::string(p->login_id) + "\'";
				retcode = SQLExecDirectA(hstmt, (SQLCHAR*)str.c_str(), SQL_NTS);
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					SQLSMALLINT pos_x, pos_y;
					SQLLEN l_px = 0, l_py = 0;

					// Bind columns 1, 2
					retcode = SQLBindCol(hstmt, 1, SQL_C_SHORT, &pos_x, 10, &l_px);
					retcode = SQLBindCol(hstmt, 2, SQL_C_SHORT, &pos_y, 10, &l_py);

					retcode = SQLFetch(hstmt);	// �Ѱ��� �о�´�.

					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {	// ����
						//cout << p->obj_id << ":" << p->login_id << " - �α��� ����" << endl;
						strcpy_s(exp->rw_buf, p->login_id);			// ����� �̸� �����ϰ�
						exp->ai_target_obj = MAKELONG(pos_x, pos_y);	// ����� ��ġ ��������
					}
					else {	// ���� (���� ID)
						//cout << p->obj_id << ":" << p->login_id << " - �α��� ����" << endl;
						exp->rw_buf[0] = 0;	// null ���ڷ� ���� ���и� �˸���.
					}
					PostQueuedCompletionStatus(handle_iocp, 1, p->obj_id, &exp->wsaover);
				}
				else {	// SQL ���� ���н� SQL_ERROR
					std::cerr << "ERROR::Query ����" << std::endl;
					disp_error(hstmt, SQL_HANDLE_STMT, retcode);
					delete exp;
					exit(-1);
				}

				// ������ ���� �� �ʱ�ȭ
				SQLFreeStmt(hstmt, SQL_CLOSE);

				break;	// switch break
			}
			case DB_EVENT::DE_SAVE: {
				DB_EVENT_SAVE* p = dynamic_cast<DB_EVENT_SAVE*>(ev.get());

				std::string str = "UPDATE user_table SET user_pos_x = " + std::to_string(p->pos_x) + ", user_pos_y = "
					+ std::to_string(p->pos_y) + " WHERE user_id = \'" + p->login_id + "\'";
				retcode = SQLExecDirectA(hstmt, (SQLCHAR*)str.c_str(), SQL_NTS);
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					std::cout << p->obj_id << ": ��ġ ���� ����" << std::endl;
				}
				else {	// SQL ���� ���н� SQL_ERROR
					std::cerr << "ERROR::Query ����" << std::endl;
					disp_error(hstmt, SQL_HANDLE_STMT, retcode);
					exit(-1);
				}

				// ������ ���� �� �ʱ�ȭ
				SQLFreeStmt(hstmt, SQL_CLOSE);

				break;	// switch break
			}

			}
		}
		else {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));	// db_queue�� ��� ������ ��� ��ٷȴٰ� �ٽ� ����
		}
	}


	// ������
	SQLCancel(hstmt);
	SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
	SQLDisconnect(hdbc);
	SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
	SQLFreeHandle(SQL_HANDLE_ENV, henv);
}
