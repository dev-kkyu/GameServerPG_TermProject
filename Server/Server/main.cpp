#include <iostream>
#include <vector>
#include <thread>

#include "NetworkManager.h"

NetworkManager g_NetworkManager{ PORT_NUM };

void worker_thread()
{
	g_NetworkManager.run();
}

void do_timer()
{
	// Todo
}

void do_db()
{
	// Todo
}

int main()
{
	::setlocale(LC_ALL, "korean");		// DB ������� �ѱ� ���ؼ�. ��Ʈ��ũ ���� ��¿��� �ʿ�

	std::vector<std::thread> worker_threads;
	int num_threads = std::thread::hardware_concurrency();
	worker_threads.reserve(num_threads);
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker_thread);

	std::thread timer_thread{ do_timer };
	std::thread db_thread{ do_db };

	db_thread.join();
	timer_thread.join();

	for (auto& th : worker_threads)
		th.join();

}
