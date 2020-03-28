#ifndef CONCURRENCPP_THREAD_POOL_H
#define CONCURRENCPP_THREAD_POOL_H

#include "../executors/task.h"
#include "../utils/spinlock.h"
#include "../utils/array_deque.h"
#include "thread_pool_listener.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include <vector>

#include <string_view>

namespace concurrencpp::details {
	class thread_pool;
	class thread_pool_worker;
	class waiting_worker_stack;

	class waiting_worker_stack {
		/*
			An implementation of the simple lock-free Treiber stack.
			Treiber algorithm presents an ABA problem:

			let's say our stack looks logically like this : [Head = A] -> B -> C -> nil
			1) thread_1 reads A.next and stores it locally on the stack, A next is B. then thread_1 gets interrupted.

			2) thread_2 pops A,B and C and re-pushes them in a slightly different order :
				[Head = A] -> C -> B -> nil

			3) thread_1 continues where it stopped and CASes A. since stack's head is A ,
			and the local A.next is B, Head is now B.
			so the stack now looks like [Head = B] -> nil (C got lost)

			this might cause 2 problems, non is serious for us to handle:

			a) If ABA occures, some nodes might get lost, leading to a memory leak.
				==> Not a problem here because nodes are anyway owned by their respective
				workers. the memory is reclaimed fully when the threadpool destructor is called.
				this stack doesn't own workers' addresses.

			b) If ABA occures, some nodes might get lost. in our scenario, some waiting workers
				are lost.
				==> in owr case, we can live with it. this stack is not meant to be absolute, but a strong hint
				which threads are waiting. even if some nodes are "lost", we get other waiting threads.
				Even in the worst case were all nodes are lost, we just fall back to round-robin.
			*/

	public:
		struct node {
			thread_pool_worker* const self;
			std::atomic<node*> next;

			node(thread_pool_worker* worker) noexcept;
		};

	private:
		std::atomic<node*> m_head;

	public:
		waiting_worker_stack() noexcept;

		void push(node* worker_node) noexcept;
		thread_pool_worker* pop() noexcept;
	};

	class wait_all_node {

		/*
			in order to wait for all tasks to be executed, we wait for all threads to become waiting.
			when curr_waiting == max_workers then task_count == 0.
		*/

	private:
		std::mutex m_counter_lock;
		size_t m_curr_waiting;
		const size_t m_max_workers;
		std::condition_variable m_counter_condition;

	public:
		wait_all_node(size_t max_workers);

		void thread_waiting();
		void thread_running();

		void wait_all();
		bool wait_all(std::chrono::milliseconds ms);
	};

	class thread_pool_worker {

		enum class action_status {
			tasks_available,
			no_tasks,
			shutdown_requested
		};

		enum class worker_status {

			/*
			c.tor/d.tor <==>  idle <===> running
								^		   |
								|		   V
								------- shutdown
			*/

			idle,
			running,
			shutdown
		};

	private:
		std::mutex m_lock;
		worker_status m_status;
		array_deque<task> m_tasks;
		std::condition_variable m_condition;
		std::thread m_thread;
		std::thread::id m_thread_id; //cached m_thread id.
		thread_pool& m_parent_pool;
		waiting_worker_stack::node m_node;

		static thread_local thread_pool_worker* tl_self_ptr;

		void assert_self_thread() const noexcept;
		void assert_idle_thread() const noexcept;

		action_status drain_local_queue();
		action_status try_steal_task();

		action_status wait_for_task();
		action_status wait_for_task_impl(std::unique_lock<std::mutex>& lock);

		void exit(std::unique_lock<std::mutex>& lock);

		void dispose_worker(std::thread thread);
		void activate_worker(std::unique_lock<std::mutex>& lock);

		void work_loop() noexcept;

	public:
		thread_pool_worker(thread_pool& parent_pool) noexcept;
		thread_pool_worker(thread_pool_worker&&) noexcept;

		~thread_pool_worker() noexcept;

		void signal_termination();
		void join();

		std::thread::id enqueue_if_empty(task& task);
		std::thread::id enqueue(task& task, bool self);
		task try_donate_task() noexcept;

		void cancel_pending_tasks(std::exception_ptr reason) noexcept;

		static thread_pool_worker* this_thread_as_worker() noexcept;
		const thread_pool& get_parent_pool() const noexcept;
		waiting_worker_stack::node* get_waiting_node() noexcept;
	};

	class thread_pool {

	private:
		waiting_worker_stack m_waiting_workers;
		std::vector<thread_pool_worker> m_workers;
		std::atomic_size_t m_round_robin_index;
		wait_all_node m_wait_all_node;
		const size_t m_max_workers;
		const std::string_view m_cancellation_msg;
		const std::shared_ptr<thread_pool_listener_base> m_listener;
		const std::chrono::seconds m_max_waiting_time;

	public:
		thread_pool(
			size_t max_worker_count,
			std::chrono::seconds max_waiting_time,
			std::string_view cancellation_msg,
			std::shared_ptr<thread_pool_listener_base> listener_ptr);

		~thread_pool() noexcept;

		std::thread::id enqueue(task task);

		void wait_all();
		bool wait_all(std::chrono::milliseconds ms);

		//called by the workers
		bool try_steal_task(thread_pool_worker& worker);

		void mark_thread_running() noexcept;
		void mark_thread_waiting(thread_pool_worker& waiting_thread) noexcept;

		const std::shared_ptr<thread_pool_listener_base>& get_listener() const noexcept;
		std::chrono::seconds max_waiting_time() const noexcept;
	};
}

#endif //CONCURRENCPP_THREAD_POOL_H