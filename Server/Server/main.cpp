#include <iostream>
#include <vector>
#include <thread>

#include "NetworkManager.h"

NetworkManager g_NetworkManager{ PORT_NUM };

void worker_thread()
{
	g_NetworkManager.runWorker();
}

void do_timer()
{
	g_NetworkManager.runTimer();
}

void do_db()
{
	g_NetworkManager.runDB();
}

int main()
{
	::setlocale(LC_ALL, "korean");		// DB 오류출력 한글 위해서. 네트워크 오류 출력에도 필요

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
