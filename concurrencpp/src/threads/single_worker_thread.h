#ifndef CONCURRENCPP_SINGLE_WORKER_THREAD_H
#define CONCURRENCPP_SINGLE_WORKER_THREAD_H

#include "../executors/task.h"
#include "../utils/array_deque.h"

#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>

namespace concurrencpp::details {
	class single_worker_thread {

	private:
		std::mutex m_lock;
		array_deque<task> m_tasks;
		std::condition_variable m_push_condition;
		std::condition_variable m_pop_condition;
		std::thread m_thread;
		const std::string m_cancellation_error_msg;
		bool m_cancelled;

		void join();
		void cancel_all_tasks();

		void work_loop() noexcept;

	public:
		single_worker_thread(std::string_view cancellation_error_msg);
		~single_worker_thread() noexcept;

		void enqueue(task task);

		void wait_all();
		bool wait_all(std::chrono::milliseconds ms);
	};
}

#endif