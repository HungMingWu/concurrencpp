#include "thread_group.h"

#include <algorithm>

#include <cassert>

namespace concurrencpp::details {
	class thread_group_worker {

	private:
		task m_task;
		std::thread m_thread;
		thread_group& m_parent_pool;
		typename std::list<thread_group_worker>::iterator m_self_it;

		void execute_and_retire();

	public:
		thread_group_worker(task& function, thread_group& parent_pool) noexcept;
		~thread_group_worker() noexcept;

		std::thread::id get_id() const noexcept;
		std::thread::id start(std::list<thread_group_worker>::iterator self_it);
	};
}

using concurrencpp::task;
using concurrencpp::details::thread_group;
using concurrencpp::details::thread_group_worker;
using concurrencpp::details::thread_pool_listener_base;

using listener_ptr = std::shared_ptr<thread_pool_listener_base>;

thread_group_worker::thread_group_worker(task& function, thread_group& parent_pool) noexcept :
	m_task(std::move(function)),
	m_parent_pool(parent_pool) {}

thread_group_worker::~thread_group_worker() noexcept {
	m_thread.join();
}

void thread_group_worker::execute_and_retire() {
	m_task();
	m_task.clear();
	m_parent_pool.retire_worker(m_self_it);
}

std::thread::id thread_group_worker::get_id() const noexcept {
	return m_thread.get_id();
}

std::thread::id thread_group_worker::start(std::list<thread_group_worker>::iterator self_it) {
	m_self_it = self_it;
	m_thread = std::thread([this] { execute_and_retire(); });
	return m_thread.get_id();
}

thread_group::thread_group(listener_ptr listener) :
	m_listener(std::move(listener)) {}

thread_group::~thread_group() noexcept {
	wait_all();

	assert(m_workers.empty());
	clear_last_retired(std::move(m_last_retired));
}

std::thread::id thread_group::enqueue(task callable) {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	auto& new_worker = m_workers.emplace_back(callable, *this);
	auto worker_it = std::prev(m_workers.end());
	const auto id = new_worker.start(worker_it);

	const auto& listener = get_listener();
	if (static_cast<bool>(listener)) {
		listener->on_thread_created(id);
	}

	return id;
}

void concurrencpp::details::thread_group::wait_all() {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	m_condition.wait(lock, [this] { return m_workers.empty(); });
}

bool concurrencpp::details::thread_group::wait_all(std::chrono::milliseconds ms) {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	return m_condition.wait_for(lock, ms, [this] { return m_workers.empty(); });
}

const listener_ptr& thread_group::get_listener() const noexcept {
	return m_listener;
}

void thread_group::clear_last_retired(std::list<thread_group_worker> last_retired) {
	if (last_retired.empty()) {
		return;
	}

	assert(last_retired.size() == 1);
	const auto thread_id = last_retired.front().get_id();

	last_retired.clear();

	const auto& listener = get_listener();
	if (static_cast<bool>(listener)) {
		listener->on_thread_destroyed(thread_id);
	}
}

void thread_group::retire_worker(std::list<thread_group_worker>::iterator it) {
	std::list<thread_group_worker> last_retired;
	const auto id = it->get_id();

	std::unique_lock<decltype(m_lock)> lock(m_lock);
	last_retired = std::move(m_last_retired);
	m_last_retired.splice(m_last_retired.begin(), m_workers, it);

	lock.unlock();
	m_condition.notify_one();

	const auto& listener = get_listener();
	if (static_cast<bool>(listener)) {
		listener->on_thread_idling(id);
	}

	clear_last_retired(std::move(last_retired));
}