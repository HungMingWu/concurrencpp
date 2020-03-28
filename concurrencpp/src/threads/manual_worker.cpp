#include "manual_worker.h"
#include "../errors.h"

concurrencpp::details::manual_worker::manual_worker(std::string_view cancellation_msg) :
	m_cancellation_msg(cancellation_msg) {}

concurrencpp::details::manual_worker::~manual_worker() noexcept {
	concurrencpp::errors::broken_task error(m_cancellation_msg);
	const auto exception_ptr = std::make_exception_ptr(error);

	cancel_all(exception_ptr);
}

void concurrencpp::details::manual_worker::enqueue(concurrencpp::task task) {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	m_tasks.emplace_back(std::move(task));

	lock.unlock();
	m_push_condition.notify_all();
}

size_t concurrencpp::details::manual_worker::size() const noexcept {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	return m_tasks.size();
}

bool concurrencpp::details::manual_worker::empty() const noexcept {
	return size() == 0;
}

bool concurrencpp::details::manual_worker::loop_once() {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	if (m_tasks.empty()) {
		return false;
	}

	auto task = m_tasks.pop_front();
	const auto task_queue_is_empty = m_tasks.empty();

	lock.unlock();

	task();

	if (task_queue_is_empty) {
		m_pop_condition.notify_all();
	}

	return true;
}

size_t concurrencpp::details::manual_worker::loop(size_t counts) {
	size_t executed = 0;
	for (; executed < counts; ++executed) {
		if (!loop_once()) {
			break;
		}
	}

	return executed;
}

void concurrencpp::details::manual_worker::cancel_all(std::exception_ptr reason) {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	for (auto& task : m_tasks) {
		task.cancel(reason);
	}

	m_tasks.clear();
}

void concurrencpp::details::manual_worker::wait_for_task() {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	m_push_condition.wait(lock, [this] { return !m_tasks.empty(); });
	assert(!m_tasks.empty());
}

bool concurrencpp::details::manual_worker::wait_for_task(std::chrono::milliseconds max_waiting_time) {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	return m_push_condition.wait_for(lock, max_waiting_time, [this] { return !m_tasks.empty(); });
}

void concurrencpp::details::manual_worker::wait_all() {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	m_pop_condition.wait(lock, [this] { return m_tasks.empty(); });
}

bool concurrencpp::details::manual_worker::wait_all(std::chrono::milliseconds ms) {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	return m_pop_condition.wait_for(lock, ms, [this] { return m_tasks.empty(); });
}
