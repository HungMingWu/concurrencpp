#include "concurrencpp.h"

#include <iostream>

#include <Windows.h>

int main() {

	concurrencpp::details::single_worker_thread worker("");

	std::atomic<std::chrono::high_resolution_clock::time_point> start{ std::chrono::high_resolution_clock::now() };
	for (size_t i = 0; i < 10'024; i++) {
		worker.enqueue([] {});
	}

	worker.enqueue([s = start.load()]{
		const auto now = std::chrono::high_resolution_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s);

		std::cout << ms.count() << std::endl;
		});

	worker.wait_all();
	std::cout << "done waiting" << std::endl;
	std::getchar();


	return 0;
}
