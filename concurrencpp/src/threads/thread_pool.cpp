#include "thread_pool.h"
#include "constants.h"

#include "../errors.h"

#include <string>

#include <cassert>

using concurrencpp::task;
using concurrencpp::details::thread_pool;
using concurrencpp::details::waiting_worker_stack;
using concurrencpp::details::thread_pool_worker;
using concurrencpp::details::thread_pool_listener_base;

using listener_ptr = std::shared_ptr<thread_pool_listener_base>;

waiting_worker_stack::node::node(thread_pool_worker* worker) noexcept :
	self(worker),
	next(nullptr) {}

waiting_worker_stack::waiting_worker_stack() noexcept :
	m_head(nullptr) {}

void waiting_worker_stack::push(waiting_worker_stack::node* worker_node) noexcept {
	while (true) {
		auto head = m_head.load(std::memory_order_acquire);
		worker_node->next.store(head, std::memory_order_release);

		if (m_head.compare_exchange_weak(head, worker_node, std::memory_order_acq_rel)) {
			return;
		}
	}
}

thread_pool_worker* waiting_worker_stack::pop() noexcept {
	while (true) {
		auto head = m_head.load(std::memory_order_acquire);
		if (head == nullptr) {
			return nullptr;
		}

		auto next = head->next.load(std::memory_order_acquire);
		if (m_head.compare_exchange_weak(head, next, std::memory_order_acq_rel)) {
			return head->self;
		}
	}
}

concurrencpp::details::wait_all_node::wait_all_node(size_t max_workers) :
	m_curr_waiting(0),
	m_max_workers(max_workers) {}

void concurrencpp::details::wait_all_node::thread_waiting() {
	std::unique_lock<decltype(m_counter_lock)> lock(m_counter_lock);
	++m_curr_waiting;

	if (m_curr_waiting != m_max_workers) {
		return;
	}

	lock.unlock();
	m_counter_condition.notify_all();
}

void concurrencpp::details::wait_all_node::thread_running() {
	std::unique_lock<decltype(m_counter_lock)> lock(m_counter_lock);
	assert(m_curr_waiting != 0);
	--m_curr_waiting;
}

void concurrencpp::details::wait_all_node::wait_all() {
	std::unique_lock<decltype(m_counter_lock)> lock(m_counter_lock);
	m_counter_condition.wait(lock, [this] {
		return m_curr_waiting == m_max_workers;
	});

	assert(m_curr_waiting == m_max_workers);
}

bool concurrencpp::details::wait_all_node::wait_all(std::chrono::milliseconds ms) {
	std::unique_lock<decltype(m_counter_lock)> lock(m_counter_lock);
	return m_counter_condition.wait_for(lock, ms, [this] {
		return m_curr_waiting == m_max_workers;
	});
}

thread_local thread_pool_worker* thread_pool_worker::tl_self_ptr = nullptr;

thread_pool_worker::thread_pool_worker(thread_pool& parent_pool) noexcept :
	m_status(worker_status::idle),
	m_parent_pool(parent_pool),
	m_node(this) {}

thread_pool_worker::thread_pool_worker(thread_pool_worker&& rhs) noexcept :
	m_parent_pool(rhs.m_parent_pool),
	m_node(this) {
	std::abort(); //shoudn't be called!
}

thread_pool_worker::~thread_pool_worker() noexcept {
	assert(!m_thread.joinable());
	assert_idle_thread();
}

void thread_pool_worker::assert_self_thread() const noexcept {
	assert(m_status != worker_status::idle);
	assert(m_thread.joinable());
	assert(m_thread.get_id() == std::this_thread::get_id());
	assert(m_thread_id == m_thread.get_id());
}

void thread_pool_worker::assert_idle_thread() const noexcept {
	assert(m_status == worker_status::idle);
	assert(m_tasks.empty());
	assert(m_thread_id == std::thread::id());
	//m_thread might be joinable and might not. joinable != running.
}

thread_pool_worker::action_status thread_pool_worker::drain_local_queue() {
	while (true) {
		std::unique_lock<decltype(m_lock)> lock(m_lock);
		if (m_status == worker_status::shutdown) {
			return action_status::shutdown_requested;
		}

		assert_self_thread();

		if (m_tasks.empty()) {
			break;
		}

		auto task = m_tasks.pop_front();
		lock.unlock();

		task();
	}

	return action_status::no_tasks;
}

thread_pool_worker::action_status thread_pool_worker::try_steal_task() {
	return m_parent_pool.try_steal_task(*this) ? action_status::tasks_available : action_status::no_tasks;
}

thread_pool_worker::action_status thread_pool_worker::wait_for_task_impl(std::unique_lock<std::mutex>& lock) {
	assert(lock.owns_lock());
	assert_self_thread();

	const auto listener = m_parent_pool.get_listener();
	const auto time_to_wait = m_parent_pool.max_waiting_time();

	if (static_cast<bool>(listener)) {
		listener->on_thread_waiting(std::this_thread::get_id());
	}

	m_condition.wait_for(lock, time_to_wait, [this] {
		return !m_tasks.empty() || (m_status == worker_status::shutdown);
	});

	if (m_status == worker_status::shutdown) {
		return action_status::shutdown_requested;
	}

	const auto task_found = !m_tasks.empty();

	if (static_cast<bool>(listener)) {
		if (task_found) {
			listener->on_thread_resuming(std::this_thread::get_id());
		}
		else {
			listener->on_thread_idling(std::this_thread::get_id());
		}
	}

	return task_found ? action_status::tasks_available : action_status::no_tasks;
}

thread_pool_worker::action_status thread_pool_worker::wait_for_task() {
	m_parent_pool.mark_thread_waiting(*this);

	std::unique_lock<decltype(m_lock)> lock(m_lock);
	const auto status = wait_for_task_impl(lock);

	switch (status)
	{

	case action_status::tasks_available: {
		m_parent_pool.mark_thread_running();
		break;
	}

	case action_status::no_tasks:
	case action_status::shutdown_requested: {
		exit(lock);
		break;
	}

	}

	return status;
}

void thread_pool_worker::exit(std::unique_lock<std::mutex>& lock) {
	assert(lock.owns_lock());
	assert_self_thread();

	m_status = worker_status::idle;
	m_thread_id = std::thread::id();

	lock.unlock();
	m_condition.notify_all();
}

void thread_pool_worker::work_loop() noexcept {
	tl_self_ptr = this;
	m_parent_pool.mark_thread_running();

	while (true) {
		if (drain_local_queue() == action_status::shutdown_requested) {
			std::unique_lock<decltype(m_lock)> lock(m_lock);
			return exit(lock);
		}

		//no local tasks, try to steal some
		if (try_steal_task() == action_status::tasks_available) {
			continue;
		}

		//no tasks to steal, wait
		if (wait_for_task() != action_status::tasks_available) {
			return;
		}
	}
}

void concurrencpp::details::thread_pool_worker::signal_termination() {
	{
		std::unique_lock<decltype(m_lock)> lock(m_lock);
		if (m_status == worker_status::idle) {
			assert_idle_thread();
			return; //nothing to terminate
		}

		m_status = worker_status::shutdown;
	}

	m_condition.notify_all();
}

void thread_pool_worker::join() {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	m_condition.wait(lock, [this] { return m_status == worker_status::idle; });
	
	assert(m_status == worker_status::idle);
	assert(m_thread_id == std::thread::id());
	dispose_worker(std::move(m_thread));
}

void thread_pool_worker::dispose_worker(std::thread worker_thread) {
	if (!worker_thread.joinable()) {
		return;
	}

	const auto id = worker_thread.get_id();
	worker_thread.join();

	const auto& listener = m_parent_pool.get_listener();
	if (static_cast<bool>(listener)) {
		listener->on_thread_destroyed(id);
	}
}

void thread_pool_worker::activate_worker(std::unique_lock<std::mutex>& lock) {
	assert(lock.owns_lock());
	assert(m_status == worker_status::idle);
	assert(m_thread_id == std::thread::id());

	//m_worker might be a real idle thread, not a defaultly constructed thread. in this case, we need to join it.
	auto thread = std::move(m_thread);

	m_thread = std::thread([this] { work_loop(); });
	m_thread_id = m_thread.get_id();
	m_status = worker_status::running;

	const auto listener = m_parent_pool.get_listener();
	if (static_cast<bool>(listener)) {
		listener->on_thread_created(m_thread_id);
	}

	lock.unlock();
	dispose_worker(std::move(thread));
}

std::thread::id thread_pool_worker::enqueue_if_empty(task& task) {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	assert_self_thread();

	if (m_tasks.empty()) {
		m_tasks.emplace_back(std::move(task));
		return m_thread_id;
	}

	return {};
}

std::thread::id thread_pool_worker::enqueue(task& task, bool self) {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	m_tasks.emplace_back(std::move(task));

	if (self) {
		assert_self_thread();
		return m_thread_id;
	}

	if (m_status == worker_status::idle) {
		activate_worker(lock);
		return m_thread_id;
	}

	const auto worker_id = m_thread_id;
	lock.unlock();
	m_condition.notify_one();

	return worker_id;
}

task thread_pool_worker::try_donate_task() noexcept {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	if (!m_tasks.empty()) {
		return m_tasks.pop_front();
	}

	return {};
}

void thread_pool_worker::cancel_pending_tasks(std::exception_ptr reason) noexcept {
	std::unique_lock<decltype(m_lock)> lock(m_lock);
	for (auto& task : m_tasks) {
		task.cancel(reason);
	}

	m_tasks.clear();
}

thread_pool_worker* thread_pool_worker::this_thread_as_worker() noexcept {
	return tl_self_ptr;
}

waiting_worker_stack::node* thread_pool_worker::get_waiting_node() noexcept {
	return std::addressof(m_node);
}

thread_pool::thread_pool(
	size_t max_workers_allowed,
	std::chrono::seconds max_waiting_time,
	std::string_view cancellation_msg,
	std::shared_ptr<thread_pool_listener_base> listener) :
	m_round_robin_index(0),
	m_wait_all_node(max_workers_allowed),
	m_max_workers(max_workers_allowed),
	m_cancellation_msg(cancellation_msg),
	m_listener(std::move(listener)),
	m_max_waiting_time(max_waiting_time) {

	m_workers.reserve(max_workers_allowed);
	for (size_t i = 0; i < max_workers_allowed; i++) {
		m_workers.emplace_back(*this);
	}

	for (auto& worker : m_workers) {
		mark_thread_waiting(worker);
	}
}

thread_pool::~thread_pool() noexcept {
	for (auto& worker : m_workers) {
		worker.signal_termination();
	}

	std::this_thread::yield();

	for (auto& worker : m_workers) {
		worker.join();
	}

	concurrencpp::errors::broken_task error(m_cancellation_msg.data());
	auto reason = std::make_exception_ptr(error);

	for (auto& worker : m_workers) {
		worker.cancel_pending_tasks(reason);
	}
}

std::thread::id thread_pool::enqueue(task task) {
	auto this_thread_as_worker = thread_pool_worker::this_thread_as_worker();
	if (this_thread_as_worker != nullptr) {
		const auto worker_id = this_thread_as_worker->enqueue_if_empty(task);
		if (worker_id != std::thread::id()) {
			return worker_id;
		}
	}

	auto waiting_worker = m_waiting_workers.pop();
	if (waiting_worker != nullptr) {
		return waiting_worker->enqueue(task, false);
	}

	if (this_thread_as_worker != nullptr) {
		return this_thread_as_worker->enqueue(task, true);
	}

	const auto round_robin_index = m_round_robin_index.fetch_add(1, std::memory_order_acq_rel) % m_max_workers;
	auto& worker = m_workers[round_robin_index];
	return worker.enqueue(task, false);
}

void thread_pool::wait_all() {
	m_wait_all_node.wait_all();
}

bool thread_pool::wait_all(std::chrono::milliseconds ms) {
	return m_wait_all_node.wait_all(ms);
}

bool thread_pool::try_steal_task(thread_pool_worker& worker) {
	/*
		if this_thread's index is index0, try to steal from range [index0 + 1, ..., m_workers.size() -1]
		if no task was found, try steam from range [0, ..., index0 - 1]. this ensure low contention and nice distribution
		of tasks across thread pool threads
	*/

	auto try_steal_impl = [&](size_t index) noexcept {
		auto task = m_workers[index].try_donate_task();

		if (!static_cast<bool>(task)) {
			return false;
		}

		task();
		return true;
	};

	assert(std::addressof(worker) >= m_workers.data());
	assert(std::addressof(worker) < m_workers.data() + m_workers.size());
	const auto worker_index = static_cast<size_t>(std::addressof(worker) - m_workers.data());

	for (size_t i = worker_index + 1; i < m_workers.size(); i++) {
		if (try_steal_impl(i)) {
			return true;
		}
	}

	for (size_t i = 0; i < worker_index; i++) {
		if (try_steal_impl(i)) {
			return true;
		}
	}

	return false;
}

void thread_pool::mark_thread_waiting(thread_pool_worker& waiting_thread) noexcept {
	m_waiting_workers.push(waiting_thread.get_waiting_node());
	m_wait_all_node.thread_waiting();
}

void thread_pool::mark_thread_running() noexcept {
	m_wait_all_node.thread_running();
}

const listener_ptr& thread_pool::get_listener() const noexcept {
	return m_listener;
}

std::chrono::seconds thread_pool::max_waiting_time() const noexcept {
	return m_max_waiting_time;
}