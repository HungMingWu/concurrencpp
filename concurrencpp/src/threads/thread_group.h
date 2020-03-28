#ifndef CONCURRENCPP_THREAD_GROUP_H
#define CONCURRENCPP_THREAD_GROUP_H

#include "../executors/task.h"
#include "thread_pool_listener.h"

#include <list>
#include <mutex>
#include <memory>
#include <condition_variable>

#include <string_view>

namespace concurrencpp::details {
	class thread_group_worker;

	class thread_group {

	private:
		std::mutex m_lock;
		std::list<thread_group_worker> m_workers;
		std::condition_variable m_condition;
		const std::shared_ptr<thread_pool_listener_base> m_listener;
		std::list<thread_group_worker> m_last_retired;

		void enqueue_worker(task& callable);
		void clear_last_retired(std::list<thread_group_worker> last_retired);

	public:
		thread_group(std::shared_ptr<thread_pool_listener_base> listener_ptr);
		~thread_group() noexcept;

		std::thread::id enqueue(task task);

		void wait_all();
		bool wait_all(std::chrono::milliseconds ms);

		void retire_worker(std::list<thread_group_worker>::iterator it);
		const std::shared_ptr<thread_pool_listener_base>& get_listener() const noexcept;
	};
}

#endif //CONCURRENCPP_THREAD_GROUP_H