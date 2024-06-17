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
	: gameFramework{ handle_iocp, timer_queue, db_queue }
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
		case EXP_OVERLAPPED::OP_AI_BYE: {
			gameFramework.npcBye(exp_over->ai_target_obj, static_cast<int>(key));
			delete exp_over;
			break;
		}
		case EXP_OVERLAPPED::OP_PLAYER_MOVE: {		// �÷��̾ �ѹ� �̵��� ������ ��� NPC���� �ߵ�..	cs_move->wakeup..
			gameFramework.callbackPlayerMove(static_cast<int>(key), exp_over->ai_target_obj);
			delete exp_over;
			break;
		}
		case EXP_OVERLAPPED::OP_RESPAWN: {
			gameFramework.doRespawn(static_cast<int>(key));
			delete exp_over;
			break;
		}
		case EXP_OVERLAPPED::OP_HP_CHARGE: {
			gameFramework.doHpCharge(static_cast<int>(key));
			delete exp_over;
			break;
		}

		case EXP_OVERLAPPED::OP_DB_LOGIN: {
			EXP_EXP_OVER* ex = reinterpret_cast<EXP_EXP_OVER*>(exp_over);
			gameFramework.callbackDBLogin(static_cast<int>(key), ex);
			delete ex;
			break;
		}

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
			case TIMER_EVENT::TE_RESPAWN: {
				EXP_OVERLAPPED* ov = new EXP_OVERLAPPED{ EXP_OVERLAPPED::OP_RESPAWN };
				PostQueuedCompletionStatus(handle_iocp, 1, ev.obj_id, &ov->wsaover);
				break;
			}
			case TIMER_EVENT::TE_HP_CHARGE: {
				EXP_OVERLAPPED* ov = new EXP_OVERLAPPED{ EXP_OVERLAPPED::OP_HP_CHARGE };
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
	retcode = SQLConnect(hdbc, (SQLWCHAR*)L"2020180051_GS_TERMPROJECT", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);
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

				std::string str = "EXEC GetUser @user_id = \'" + std::string(p->login_id) + "\';";
				retcode = SQLExecDirectA(hstmt, (SQLCHAR*)str.c_str(), SQL_NTS);
				if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {
					SQLSMALLINT pos_x, pos_y, level, max_hp, hp, exp;
					SQLLEN l_px = 0, l_py = 0, l_level = 0, l_max_hp = 0, l_hp = 0, l_exp = 0;

					// Bind columns 1, 2
					retcode = SQLBindCol(hstmt, 1, SQL_C_SHORT, &pos_x, 10, &l_px);
					retcode = SQLBindCol(hstmt, 2, SQL_C_SHORT, &pos_y, 10, &l_py);
					retcode = SQLBindCol(hstmt, 3, SQL_C_SHORT, &level, 10, &l_level);
					retcode = SQLBindCol(hstmt, 4, SQL_C_SHORT, &max_hp, 10, &l_max_hp);
					retcode = SQLBindCol(hstmt, 5, SQL_C_SHORT, &hp, 10, &l_hp);
					retcode = SQLBindCol(hstmt, 6, SQL_C_SHORT, &exp, 10, &l_exp);

					retcode = SQLFetch(hstmt);	// �Ѱ��� �о�´�.

					EXP_EXP_OVER* ex = new EXP_EXP_OVER{ p->login_id, pos_x, pos_y, level, max_hp, hp, exp };
					if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) {	// ����
						//cout << p->obj_id << ":" << p->login_id << " - �α��� ����" << endl;
						strcpy_s(ex->exp_over.rw_buf, p->login_id);			// ����� �̸� �����ϰ�
						ex->x = pos_x;
						ex->y = pos_y;
						ex->level = level;
						ex->max_hp = max_hp;
						ex->hp = hp;
						ex->exp = exp;
					}
					else {	// ���� (���� ID)
						//cout << p->obj_id << ":" << p->login_id << " - �α��� ����" << endl;
						ex->exp_over.rw_buf[0] = 0;	// null ���ڷ� ���� ���и� �˸���.
					}
					PostQueuedCompletionStatus(handle_iocp, 1, p->obj_id, &ex->exp_over.wsaover);
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
			case DB_EVENT::DE_SAVE: {
				DB_EVENT_SAVE* p = dynamic_cast<DB_EVENT_SAVE*>(ev.get());

				//std::string str = "UPDATE user_table SET user_pos_x = " + std::to_string(p->pos_x) + ", user_pos_y = "
				//	+ std::to_string(p->pos_y) + " WHERE user_id = \'" + p->login_id + "\'";
				std::string str = "EXEC UpdateUser @user_id = \'" + std::string(p->login_id) + "\', @user_pos_x = " + std::to_string(p->pos_x) +
					", @user_pos_y = " + std::to_string(p->pos_y) + ", @user_level = " + std::to_string(p->level) + ", @user_max_hp = " + std::to_string(p->max_hp) +
					", @user_hp = " + std::to_string(p->hp) + ", @user_exp = " + std::to_string(p->exp) + ";";
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
