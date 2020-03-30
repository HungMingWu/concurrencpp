#ifndef CONCURRENCPP_EXECUTORS_H
#define CONCURRENCPP_EXECUTORS_H

#include "executor.h"

#include "../forward_declerations.h"
#include "../threads/thread_pool.h"
#include "../threads/thread_group.h"
#include "../threads/single_worker_thread.h"
#include "../threads/manual_worker.h"

#include <memory>
#include <string_view>
#include <type_traits>

namespace concurrencpp {
	class inline_executor final : public executor {

		friend class ::concurrencpp::runtime;

		struct context {};

	public:
		inline_executor(const inline_executor::context&) noexcept;

		std::string_view name() const noexcept override;
		void enqueue(task task) override;

		void wait_all() override;
		bool wait_all(std::chrono::milliseconds ms) override;
	};

	class thread_pool_executor final : public executor {

		friend class ::concurrencpp::runtime;

		struct context {
			std::string_view cancellation_msg;
			size_t max_worker_count;
			std::chrono::seconds max_waiting_time;
		};

	private:
		details::thread_pool m_cpu_thread_pool;

	public:
		thread_pool_executor(const thread_pool_executor::context&);

		std::string_view name() const noexcept override;
		void enqueue(task task) override;

		void wait_all() override;
		bool wait_all(std::chrono::milliseconds ms) override;
	};

	class background_executor final : public executor {

		friend class ::concurrencpp::runtime;

		struct context {
			std::string_view cancellation_msg;
			size_t max_worker_count;
			std::chrono::seconds max_waiting_time;
		};

	private:
		details::thread_pool m_background_thread_pool;

	public:
		background_executor(const background_executor::context&);

		std::string_view name() const noexcept override;
		void enqueue(task task) override;

		void wait_all() override;
		bool wait_all(std::chrono::milliseconds ms) override;
	};

	class thread_executor final : public executor {

		friend class ::concurrencpp::runtime;

		struct context {};

	private:
		details::thread_group m_thread_group;

	public:
		thread_executor(const thread_executor::context&) noexcept;

		std::string_view name() const noexcept override;
		void enqueue(task task) override;

		void wait_all() override;
		bool wait_all(std::chrono::milliseconds ms) override;
	};

	class worker_thread_executor final : public executor {

		friend class ::concurrencpp::runtime;

	private:
		details::single_worker_thread m_worker;

		struct context {};

	public:
		worker_thread_executor(const worker_thread_executor::context&);

		std::string_view name() const noexcept override;
		void enqueue(task task) override;

		void wait_all() override;
		bool wait_all(std::chrono::milliseconds ms) override;
	};

	class manual_executor final : public executor {

		friend class ::concurrencpp::runtime;

		struct context {};

	private:
		details::manual_worker m_worker;

	public:
		manual_executor(const manual_executor::context&);

		std::string_view name() const noexcept override;
		void enqueue(task task) override;

		size_t size() const noexcept;
		bool empty() const noexcept;

		bool loop_once();
		size_t loop(size_t counts);

		void cancel_all(std::exception_ptr reason);

		void wait_for_task();
		bool wait_for_task(std::chrono::milliseconds ms);

		void wait_all() override;
		bool wait_all(std::chrono::milliseconds ms) override;
	};
}

#endif //CONCURRENCPP_EXECUTORS_H