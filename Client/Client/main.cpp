#define SFML_STATIC 1
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <string>
#include <unordered_map>
#include <Windows.h>
#include <chrono>
using namespace std;

#include "..\..\Server\Server\protocol_2024.h"

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH = 20;
constexpr auto SCREEN_HEIGHT = 20;			//	20 * 20 윈도우

constexpr auto TILE_WIDTH = 32;
constexpr auto SCALE_WIDTH = 1.4f;
constexpr int WINDOW_WIDTH = SCREEN_WIDTH * (TILE_WIDTH * SCALE_WIDTH);   // size of window
constexpr int WINDOW_HEIGHT = SCREEN_WIDTH * (TILE_WIDTH * SCALE_WIDTH);

int g_left_x;
int g_top_y;
int g_myid;

int		g_my_hp;
int		g_my_max_hp;
int		g_my_exp;
int		g_my_level;

sf::RenderWindow* g_window;
sf::Font g_font;

sf::Text g_Text;	// 채팅 입력용
sf::RectangleShape g_Rect;
bool isEntered = false;

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;
	sf::Sprite m_attack_sprite;

	sf::Text m_name;
	sf::Text m_chat;
	chrono::steady_clock::time_point m_mess_end_time;
	chrono::steady_clock::time_point m_attack_end_time;
public:
	int id;
	int m_x, m_y;
	char name[NAME_SIZE];
	OBJECT(sf::Texture& t, int x, int y, int x2, int y2, sf::Texture& effect) {		//x, y는 시작위치, x2, y2는 거기서부터 픽셀 수
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));		// 스프라이트 픽셀 사이즈 선택
		m_sprite.setScale(sf::Vector2f(float(TILE_WIDTH) / x2 * SCALE_WIDTH, float(TILE_WIDTH) / y2 * SCALE_WIDTH));	// 화면에 그릴 사이즈
		set_name("NONAME");
		m_mess_end_time = chrono::steady_clock::now();
		m_attack_end_time = chrono::steady_clock::now();

		m_attack_sprite.setTexture(effect);
		m_attack_sprite.setTextureRect(sf::IntRect(30, 351, 16, 15));		// 스프라이트 픽셀 사이즈 선택
		m_attack_sprite.setScale(sf::Vector2f(float(TILE_WIDTH) / 16 * SCALE_WIDTH, float(TILE_WIDTH) / 15 * SCALE_WIDTH));	// 화면에 그릴 사이즈
	}
	OBJECT() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * (TILE_WIDTH * SCALE_WIDTH) + 1;		// rx, ry는 좌상단 위치
		float ry = (m_y - g_top_y) * (TILE_WIDTH * SCALE_WIDTH) + 1;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		auto size = m_name.getGlobalBounds();
		if (m_mess_end_time < chrono::steady_clock::now()) {
			m_name.setPosition(rx + 32 - size.width / 2, ry - 20);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx + 32 - size.width / 2, ry - 20);
			g_window->draw(m_chat);
		}

		if (m_attack_end_time > chrono::steady_clock::now()) {					// 공격이펙트
			m_attack_sprite.setPosition(rx - (TILE_WIDTH * SCALE_WIDTH), ry);
			g_window->draw(m_attack_sprite);
			m_attack_sprite.setPosition(rx, ry - (TILE_WIDTH * SCALE_WIDTH));
			g_window->draw(m_attack_sprite);
			m_attack_sprite.setPosition(rx + (TILE_WIDTH * SCALE_WIDTH), ry);
			g_window->draw(m_attack_sprite);
			m_attack_sprite.setPosition(rx, ry + (TILE_WIDTH * SCALE_WIDTH));
			g_window->draw(m_attack_sprite);
		}
	}
	void set_name(const char str[]) {
		strcpy_s(name, str);
		m_name.setFont(g_font);
		m_name.setString(str);
		if (id < MAX_USER) m_name.setFillColor(sf::Color(0, 0, 0));		// 플레이어 id는 검정으로 표시
		else m_name.setFillColor(sf::Color(255, 0, 255));				// NPC는 자홍색
		m_name.setStyle(sf::Text::Bold);
		m_name.setScale(0.7f, 0.7f);
	}

	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 0, 0));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::steady_clock::now() + chrono::seconds(3);
		m_chat.setScale(0.7f, 0.7f);
	}

	void Attack() {
		m_attack_end_time = chrono::steady_clock::now() + chrono::seconds(1);	// 1초간 공격모션
	}
};

OBJECT avatar;
unordered_map <int, OBJECT> players;

OBJECT tile_1;
OBJECT tile_2;
OBJECT tile_stone;

sf::Texture* board;
sf::Texture* kirby;
sf::Texture* Beanbon;
sf::Texture* Knight;

void client_initialize()
{
	board = new sf::Texture;
	kirby = new sf::Texture;
	Beanbon = new sf::Texture;
	Knight = new sf::Texture;
	board->loadFromFile("Resources/Forest.png");
	kirby->loadFromFile("Resources/Kirby.png");
	Beanbon->loadFromFile("Resources/Beanbon.png");
	Knight->loadFromFile("Resources/Meta_Knight.png");
	if (false == g_font.loadFromFile("Resources/cour.ttf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	tile_1 = OBJECT{ *board, 38, 3, TILE_WIDTH, TILE_WIDTH, *kirby };
	tile_2 = OBJECT{ *board, 353, 3, TILE_WIDTH, TILE_WIDTH, *kirby };
	tile_stone = OBJECT{ *board, 283, 3, TILE_WIDTH, TILE_WIDTH, *kirby };
	avatar = OBJECT{ *kirby, 8, 11, 20, 19, *kirby };
	avatar.move(4, 4);

	g_Text.setFont(g_font);
	g_Text.setFillColor(sf::Color(255, 255, 255));
	g_Text.setStyle(sf::Text::Bold);
	g_Text.setPosition(50.f, 800.f);

	g_Rect.setPosition(30.f, 750.f);
	g_Rect.setSize(sf::Vector2f(400, 100));
	g_Rect.setFillColor(sf::Color(0, 0, 0, 127));
}

void client_finish()
{
	players.clear();
	delete board;
	delete kirby;
	delete Beanbon;
	delete Knight;
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[2])
	{
	case SC_LOGIN_INFO:
	{
		SC_LOGIN_INFO_PACKET* packet = reinterpret_cast<SC_LOGIN_INFO_PACKET*>(ptr);
		g_myid = packet->id;
		avatar.id = g_myid;
		avatar.move(packet->x, packet->y);
		g_left_x = packet->x - SCREEN_WIDTH / 2;
		g_top_y = packet->y - SCREEN_HEIGHT / 2;
		avatar.show();

		g_my_exp = packet->exp;
		g_my_hp = packet->hp;
		g_my_level = packet->level;
		g_my_max_hp = packet->max_hp;

		std::cout << "login success : id - " << g_myid << ", exp - " << g_my_exp << ", hp - " << g_my_hp << ", level - " << g_my_level << ", max_hp - " << g_my_max_hp << std::endl;
		break;
	}
	case SC_LOGIN_FAIL:
	{
		cout << "로그인 실패! 잘못된 ID 입니다." << endl;
		break;
	}
	case SC_ADD_OBJECT:
	{
		SC_ADD_OBJECT_PACKET* my_packet = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(ptr);
		int id = my_packet->id;

		if (id == g_myid) {				// 나
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
			avatar.show();
		}
		else if (id < MAX_USER) {		// 다른유저
			players[id] = OBJECT{ *kirby, 316, 183, 20, 19, *kirby };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		else if (id < MAX_USER + MAX_AGRO) {		// Agro NPC
			players[id] = OBJECT{ *Knight, 0, 0, 22, 22, *kirby };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		else {							// Peace NPC
			players[id] = OBJECT{ *Beanbon, 0, 0, 27, 27, *kirby };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		break;
	}
	case SC_MOVE_OBJECT:
	{
		SC_MOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_MOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
		}
		else {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}

	case SC_REMOVE_OBJECT:
	{
		SC_REMOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else {
			players.erase(other_id);
		}
		break;
	}
	case SC_CHAT:
	{
		SC_CHAT_PACKET* my_packet = reinterpret_cast<SC_CHAT_PACKET*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.set_chat(my_packet->mess);
			std::cout << "공격받음 : " << my_packet->mess << std::endl;
		}
		else {
			players[other_id].set_chat(my_packet->mess);
			if (0 == strncmp(players[other_id].name, "AGRO", 4) or 0 == strncmp(players[other_id].name, "PEACE", 5)) {
				std::cout << avatar.name << "가 " << players[other_id].name << "을 공격하여 " << g_my_level * 30 << "의 데미지를 입혔습니다." << std::endl;
			}
		}

		break;
	}
	case SC_STAT_CHANGE: {
		SC_STAT_CHANGE_PACKET* my_packet = reinterpret_cast<SC_STAT_CHANGE_PACKET*>(ptr);
		g_my_exp = my_packet->exp;
		g_my_hp = my_packet->hp;
		g_my_level = my_packet->level;
		g_my_max_hp = my_packet->max_hp;

		std::cout << "상태 변경! : id - " << g_myid << ", exp - " << g_my_exp << ", hp - " << g_my_hp << ", level - " << g_my_level << ", max_hp - " << g_my_max_hp << std::endl;

		break;
	}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

bool isMoveAble(short x, short y)		// 장애물
{
	if (x % 5 == 0 and y % 5 == 0)
		return false;
	return true;
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (io_byte > 1) {
		if (0 == in_packet_size) in_packet_size = reinterpret_cast<unsigned short*>(ptr)[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		exit(-1);
	}
	if (recv_result == sf::Socket::Disconnected) {
		wcout << L"Disconnected\n";
		exit(-1);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 1) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (not isMoveAble(tile_x, tile_y)) {
				tile_stone.a_move((TILE_WIDTH * SCALE_WIDTH) * i, (TILE_WIDTH * SCALE_WIDTH) * j);
				tile_stone.a_draw();
			}
			else if (0 == (tile_x / 3 + tile_y / 3) % 2) {
				tile_1.a_move((TILE_WIDTH * SCALE_WIDTH) * i, (TILE_WIDTH * SCALE_WIDTH) * j);
				tile_1.a_draw();
			}
			else
			{
				tile_2.a_move((TILE_WIDTH * SCALE_WIDTH) * i, (TILE_WIDTH * SCALE_WIDTH) * j);
				tile_2.a_draw();
			}
		}
	for (auto& pl : players) pl.second.draw();
	avatar.draw();

	sf::Text text;
	text.setFont(g_font);
	text.setFillColor(sf::Color(0, 0, 0));
	text.setStyle(sf::Text::Bold);
	char buf[100];
	sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	text.setString(buf);
	g_window->draw(text);

	{						// 레벨 표시하기
		sf::Text LevText;
		LevText.setFont(g_font);
		LevText.setFillColor(sf::Color::Black);
		LevText.setStyle(sf::Text::Bold);
		LevText.setCharacterSize(24);
		LevText.setPosition(WINDOW_WIDTH * 0.25f, 30.f);
		LevText.setString("LEVEL: " + to_string(g_my_level) + "	AD: " + to_string(g_my_level * 30));

		g_window->draw(LevText);
	}
	{						// 체력바 그리기
		sf::RectangleShape hpBar(sf::Vector2f(WINDOW_WIDTH * 0.5f, 20.f));
		hpBar.setPosition(WINDOW_WIDTH * 0.25f, 60.f);
		hpBar.setFillColor(sf::Color(192, 192, 192));
		g_window->draw(hpBar);					// 회색 먼저 그리고
		float hpRatio = static_cast<float>(g_my_hp) / g_my_max_hp;
		hpBar.setSize(sf::Vector2f(WINDOW_WIDTH * 0.5f * hpRatio, 20.f));
		hpBar.setFillColor(sf::Color::Red);
		g_window->draw(hpBar);					// 빨간색 그리기

		sf::Text hpText;
		hpText.setFont(g_font);
		hpText.setFillColor(sf::Color::Black);
		hpText.setStyle(sf::Text::Bold);
		hpText.setCharacterSize(18);
		hpText.setPosition(WINDOW_WIDTH * 0.25f, 55.f);
		hpText.setString("HP: " + std::to_string(g_my_hp) + " / " + std::to_string(g_my_max_hp));

		g_window->draw(hpText);
	}
	{						// 경험치바 그리기
		sf::RectangleShape expBar(sf::Vector2f(WINDOW_WIDTH * 0.5f, 15.f));
		expBar.setPosition(WINDOW_WIDTH * 0.25f, 80.f);
		expBar.setFillColor(sf::Color(192, 192, 192));
		g_window->draw(expBar);					// 회색 먼저 그리고
		int need_exp = pow(2, g_my_level - 1) * 100;
		float expRatio = static_cast<float>(g_my_exp) / need_exp;
		expBar.setSize(sf::Vector2f(WINDOW_WIDTH * 0.5f * expRatio, 15.f));
		expBar.setFillColor(sf::Color::Yellow);
		g_window->draw(expBar);					// 노란색 그리기

		sf::Text expText;
		expText.setFont(g_font);
		expText.setFillColor(sf::Color::Black);
		expText.setStyle(sf::Text::Bold);
		expText.setCharacterSize(18);
		expText.setPosition(WINDOW_WIDTH * 0.25f, 75.f);
		expText.setString("EXP: " + std::to_string(g_my_exp) + " / " + std::to_string(need_exp));

		g_window->draw(expText);
	}
	if (isEntered) {
		g_window->draw(g_Rect);
		g_window->draw(g_Text);
	}
}

void send_packet(void* packet)
{
	unsigned short* p = reinterpret_cast<unsigned short*>(packet);
	size_t sent = 0;
	s_socket.send(packet, p[0], sent);
}

int main()
{
	wcout.imbue(locale("korean"));

	string ipAddr;
	cout << "IP 주소 입력 : ";
	getline(cin, ipAddr);
	if (ipAddr.empty())
		ipAddr = "127.0.0.1";

	sf::Socket::Status status = s_socket.connect(ipAddr, PORT_NUM);
	s_socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		exit(-1);
	}

	string player_name;
	cout << "플레이어 ID 입력 : ";
	cin >> player_name;

	client_initialize();
	CS_LOGIN_PACKET p;
	p.size = sizeof(p);
	p.type = CS_LOGIN;

	//string player_name{ "P" };
	//player_name += to_string(GetCurrentProcessId());

	strcpy_s(p.name, player_name.c_str());
	send_packet(&p);
	avatar.set_name(p.name);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	auto last_move = chrono::steady_clock::now() - 1s;
	auto last_attack = chrono::steady_clock::now() - 1s;

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (event.type == sf::Event::TextEntered) {
				if (isEntered) {
					static std::string chatInput;
					if (event.text.unicode == '\r' || event.text.unicode == '\n') // 엔터 키 처리
					{
						if (not chatInput.empty()) {
							// 엔터 키 입력 시 현재 입력된 텍스트를 처리
							//std::cout << "User entered: " << chatInput << std::endl;

							// 전송
							CS_CHAT_PACKET packet;
							packet.size = sizeof(packet);
							packet.type = CS_CHAT;
							strcpy_s(packet.mess, chatInput.c_str());
							send_packet(&packet);
						}

						chatInput.clear(); // 입력 초기화
						g_Text.setString(chatInput);
						isEntered = false;
					}
					else if (event.text.unicode < 128) // ASCII 문자만 허용
					{
						if (event.text.unicode == '\b') // 백스페이스 처리
						{
							if (!chatInput.empty())
								chatInput.pop_back();
						}
						else
						{
							chatInput += static_cast<char>(event.text.unicode);
						}
						g_Text.setString(chatInput);
					}
				}
				else {
					if (event.text.unicode == '\r' || event.text.unicode == '\n') // 엔터 키 처리
						isEntered = true;
				}
			}
			if (event.type == sf::Event::KeyPressed) {
				int direction = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					direction = 2;
					break;
				case sf::Keyboard::Right:
					direction = 3;
					break;
				case sf::Keyboard::Up:
					direction = 0;
					break;
				case sf::Keyboard::Down:
					direction = 1;
					break;

				case sf::Keyboard::A: {		// 공격 키
					auto now_time = chrono::steady_clock::now();
					if (now_time >= last_attack + 1s) {		// 1초에 한번만 공격
						last_attack = now_time;
						CS_ATTACK_PACKET p;
						p.size = sizeof(p);
						p.type = CS_ATTACK;
						send_packet(&p);
						avatar.Attack();
					}
					break;
				}

				case sf::Keyboard::Escape:
					window.close();
					break;
				}
				if (-1 != direction) {
					auto now_time = chrono::steady_clock::now();
					//if (now_time >= last_move + 1s) {		// 1초에 한번만 이동
						//last_move = now_time;
					CS_MOVE_PACKET p;
					p.size = sizeof(p);
					p.type = CS_MOVE;
					p.direction = direction;
					send_packet(&p);
					//}
				}

			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}