#include "single_worker_thread.h"

#include "../errors.h"

using concurrencpp::task;
using concurrencpp::details::single_worker_thread;

single_worker_thread::single_worker_thread(std::string_view cancellation_error_msg) :
	m_cancellation_error_msg(cancellation_error_msg),
	m_cancelled(false) {}

single_worker_thread::~single_worker_thread() noexcept {
	join();
	cancel_all_tasks();
}

void concurrencpp::details::single_worker_thread::join() {
	{
		std::unique_lock<decltype(m_lock)> lock(m_lock);
		if (!m_thread.joinable()) {
			return;
		}

		m_cancelled = true;
	}

	m_push_condition.notify_one();

	assert(m_thread.joinable());
	m_thread.join();
}

void concurrencpp::details::single_worker_thread::cancel_all_tasks() {
	concurrencpp::errors::broken_task error(m_cancellation_error_msg);
	const auto exception_ptr = std::make_exception_ptr(error);

	std::unique_lock<decltype(m_lock)> lock(m_lock);
	for (auto& task : m_tasks) {
		task.cancel(exception_ptr);
	}
}

void single_worker_thread::work_loop() noexcept {
	while (true) {
		std::unique_lock<decltype(m_lock)> lock(m_lock);
		if (m_tasks.empty()) {
			m_pop_condition.notify_all();
		}

		if (m_cancelled) {
			return;
		}

		m_push_condition.wait(lock, [this] {
			return !m_tasks.empty() || m_cancelled;
		});

		assert(!m_tasks.empty() || m_cancelled);

		if (m_cancelled) {
			return;
		}

		auto task = m_tasks.pop_front();
		lock.unlock();

		assert(static_cast<bool>(task));
		task();
	}
}

void single_worker_thread::enqueue(task task) {
	{
		std::unique_lock<decltype(m_lock)> lock(m_lock);
		m_tasks.emplace_back(std::move(task));

		if (!m_thread.joinable()) {
			m_thread = std::thread([this] { work_loop(); });
			return; //no need to notify.
		}
	}

	m_push_condition.notify_one();
}

void concurrencpp::details::single_worker_thread::wait_all() {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	m_pop_condition.wait(lock, [this] { return m_tasks.empty(); });
}

bool concurrencpp::details::single_worker_thread::wait_all(std::chrono::milliseconds ms) {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	return m_pop_condition.wait_for(lock, ms, [this] { return m_tasks.empty(); });
}