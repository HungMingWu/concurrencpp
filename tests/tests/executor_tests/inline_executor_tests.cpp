#include "concurrencpp.h"
#include "../all_tests.h"

#include "../../tester/tester.h"
#include "../../helpers/assertions.h"
#include "../../helpers/object_observer.h"

#include "../../concurrencpp/src/executors/constants.h"

namespace concurrencpp::tests {
	void test_inline_executor_name();
	void test_inline_executor_enqueue();
	void test_inline_executor_wait_all();
	void test_inline_executor_timed_wait_all();
}

void concurrencpp::tests::test_inline_executor_name() {
	auto executor = concurrencpp::make_runtime()->inline_executor();
	assert_same(executor->name(), concurrencpp::details::consts::k_inline_executor_name);
}

void concurrencpp::tests::test_inline_executor_enqueue() {
	object_observer observer;
	const size_t count = 1'000;
	auto executor = concurrencpp::make_runtime()->inline_executor();

	for (size_t i = 0; i < count; i++) {
		executor->enqueue(observer.get_testing_stub());
	}

	assert_same(observer.get_execution_count(), count);
	assert_same(observer.get_destruction_count(), count);

	auto execution_map = observer.get_execution_map();

	assert_same(execution_map.size(), 1);
	assert_same(execution_map.begin()->first, std::this_thread::get_id());
}

void concurrencpp::tests::test_inline_executor_wait_all(){
	auto executor = concurrencpp::make_runtime()->inline_executor();
	object_observer observer;

	const auto task_count = 128;
	for (size_t i = 0; i < task_count; i++) {
		executor->enqueue(observer.get_testing_stub());
	}

	executor->wait_all();

	assert_same(observer.get_execution_count(), task_count);
	assert_same(observer.get_destruction_count(), task_count);
}

void concurrencpp::tests::test_inline_executor_timed_wait_all(){
	auto executor = concurrencpp::make_runtime()->inline_executor();
	object_observer observer;

	const auto task_count = 128;
	for (size_t i = 0; i < task_count; i++) {
		executor->enqueue(observer.get_testing_stub());
	}

	assert_true(executor->wait_all(std::chrono::milliseconds(10)));
	
	assert_same(observer.get_execution_count(), task_count);
	assert_same(observer.get_destruction_count(), task_count);
}

void concurrencpp::tests::test_inline_executor() {
	tester tester("inline_executor test");

	tester.add_step("name", test_inline_executor_name);
	tester.add_step("enqueue", test_inline_executor_enqueue);
	tester.add_step("wait_all", test_inline_executor_wait_all);
	tester.add_step("wait_all(ms)", test_inline_executor_timed_wait_all);

	tester.launch_test();
}
