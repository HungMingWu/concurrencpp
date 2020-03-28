#ifndef  CONCURRENCPP_MANUAL_WORKER_H
#define  CONCURRENCPP_MANUAL_WORKER_H

#include "../executors/task.h"
#include "../utils/array_deque.h"

#include <thread>
#include <mutex>
#include <condition_variable>

namespace concurrencpp::details {
	class manual_worker {

	private:
		mutable std::mutex m_lock;
		array_deque<task> m_tasks;
		std::condition_variable m_push_condition;
		std::condition_variable m_pop_condition;
		const std::string m_cancellation_msg;

	public:
		manual_worker(std::string_view cancellation_msg);
		~manual_worker() noexcept;

		void enqueue(task task);

		size_t size() const noexcept;
		bool empty() const noexcept;

		bool loop_once();
		size_t loop(size_t counts);

		void cancel_all(std::exception_ptr reason);

		void wait_for_task();
		bool wait_for_task(std::chrono::milliseconds max_waiting_time);

		void wait_all();
		bool wait_all(std::chrono::milliseconds ms);
	};
}

#endif // ! CONCURRENCPP_MANUAL_WORKER_H
