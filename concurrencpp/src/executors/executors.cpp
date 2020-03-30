#include "executors.h"
#include "constants.h"

#include "../errors.h"
#include "../threads/thread_pool.h"
#include "../threads/thread_group.h"
#include "../runtime/runtime.h"

#include <numeric>

using concurrencpp::task;
using concurrencpp::inline_executor;
using concurrencpp::thread_pool_executor;
using concurrencpp::background_executor;
using concurrencpp::thread_executor;
using concurrencpp::worker_thread_executor;
using concurrencpp::manual_executor;

using concurrencpp::details::thread_group;
using concurrencpp::details::thread_pool;

/*
	inline_executor
*/

inline_executor::inline_executor(const inline_executor::context&) noexcept {}

std::string_view inline_executor::name() const noexcept {
	return details::consts::k_inline_executor_name;
}

void inline_executor::enqueue(task task) {
	task();
}

void concurrencpp::inline_executor::wait_all() {}

bool concurrencpp::inline_executor::wait_all(std::chrono::milliseconds ms) {
	return true;
}

/*
	thread_pool_executor
*/

thread_pool_executor::thread_pool_executor(const thread_pool_executor::context& ctx) :
	m_cpu_thread_pool(ctx.max_worker_count, ctx.max_waiting_time, ctx.cancellation_msg, nullptr) {}

std::string_view thread_pool_executor::name() const noexcept {
	return details::consts::k_thread_pool_executor_name;
}

void thread_pool_executor::enqueue(task task) {
	m_cpu_thread_pool.enqueue(std::move(task));
}

void concurrencpp::thread_pool_executor::wait_all() {
	m_cpu_thread_pool.wait_all();
}

bool concurrencpp::thread_pool_executor::wait_all(std::chrono::milliseconds ms) {
	return m_cpu_thread_pool.wait_all(ms);
}

/*
	background_executor
*/

background_executor::background_executor(const background_executor::context& ctx) :
	m_background_thread_pool(ctx.max_worker_count, ctx.max_waiting_time, ctx.cancellation_msg, nullptr) {}

std::string_view background_executor::name() const noexcept {
	return details::consts::k_background_executor_name;
}

void background_executor::enqueue(task task) {
	m_background_thread_pool.enqueue(std::move(task));
}

void concurrencpp::background_executor::wait_all() {
	m_background_thread_pool.wait_all();
}

bool concurrencpp::background_executor::wait_all(std::chrono::milliseconds ms) {
	return m_background_thread_pool.wait_all(ms);
}

/*
	thread_executor
*/

thread_executor::thread_executor(const thread_executor::context&) noexcept :
	m_thread_group(nullptr) {}

std::string_view thread_executor::name() const noexcept {
	return details::consts::k_thread_executor_name;
}

void thread_executor::enqueue(task task) {
	m_thread_group.enqueue(std::move(task));
}

void concurrencpp::thread_executor::wait_all() {
	m_thread_group.wait_all();
}

bool concurrencpp::thread_executor::wait_all(std::chrono::milliseconds ms) {
	return m_thread_group.wait_all(ms);
}

/*
	worker_thread_executor
*/

worker_thread_executor::worker_thread_executor(const worker_thread_executor::context&) :
	m_worker(details::consts::k_worker_thread_executor_cancel_error_msg) {}

std::string_view worker_thread_executor::name() const noexcept {
	return details::consts::k_worker_thread_executor_name;
}

void worker_thread_executor::enqueue(task task) {
	m_worker.enqueue(std::move(task));
}

void concurrencpp::worker_thread_executor::wait_all() {
	m_worker.wait_all();
}

bool concurrencpp::worker_thread_executor::wait_all(std::chrono::milliseconds ms) {
	return m_worker.wait_all(ms);
}

/*
	manual_executor
*/

manual_executor::manual_executor(const manual_executor::context&) :
	m_worker(details::consts::k_manual_executor_cancel_error_msg) {}

std::string_view manual_executor::name() const noexcept {
	return details::consts::k_manual_executor_name;
}

void manual_executor::enqueue(task task) {
	m_worker.enqueue(std::move(task));
}

size_t manual_executor::size() const noexcept {
	return m_worker.size();
}

bool manual_executor::empty() const noexcept {
	return m_worker.empty();
}

bool manual_executor::loop_once() {
	return m_worker.loop_once();
}

size_t manual_executor::loop(size_t counts) {
	return m_worker.loop(counts);
}

void manual_executor::cancel_all(std::exception_ptr reason) {
	m_worker.cancel_all(reason);
}

void manual_executor::wait_for_task() {
	m_worker.wait_for_task();
}

bool manual_executor::wait_for_task(std::chrono::milliseconds ms) {
	return m_worker.wait_for_task(ms);
}

void concurrencpp::manual_executor::wait_all() {
	m_worker.wait_all();
}

bool concurrencpp::manual_executor::wait_all(std::chrono::milliseconds ms) {
	return m_worker.wait_all(ms);
}