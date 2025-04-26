#ifndef CONCURRENCPP_FORK_JOIN_RESULT_H
#define CONCURRENCPP_FORK_JOIN_RESULT_H

#include "concurrencpp/task.h"
#include "concurrencpp/coroutines/coroutine.h"
#include "concurrencpp/results/impl/producer_context.h"
#include "concurrencpp/results/impl/return_value_struct.h"

#include <array>
#include <memory>
#include <vector>
#include <utility>
#include <stdexcept>
#include <type_traits>

namespace concurrencpp::details {
    struct fj_final_awaiter : public suspend_always {
        template<class promise_type>
        void await_suspend(coroutine_handle<promise_type> handle) noexcept {
            return handle.promise().fj_end();
        }
    };

    class fj_result_state_base {

       protected:
        fj_awaitable_base* m_parent_awaitable;

       public:
        void fj_end() const noexcept;
        void fj_start(fj_awaitable_base* parent_awaitable) noexcept;

        suspend_always initial_suspend() const noexcept;
        fj_final_awaiter final_suspend() const noexcept;
    };

    template<class type>
    class fj_result_state : public fj_result_state_base {

       private:
        producer_context<type> m_producer;

       public:
        fork_result<type> get_return_object() noexcept {
            return fork_result<type>(coroutine_handle<fj_result_state>::from_promise(*this));
        }

        void unhandled_exception() noexcept {
            m_producer.build_exception(std::current_exception());
        }

        template<class... argument_types>
        void set_result(argument_types&&... arguments) noexcept(noexcept(type(std::forward<argument_types>(arguments)...))) {
            m_producer.build_result(std::forward<argument_types>(arguments)...);
        }

        type get() {
            return m_producer.get();
        }
    };

    template<class type>
    struct fj_promise : public fj_result_state<type>, public return_value_struct<fj_promise<type>, type> {};

    struct fj_helper {
        template<class type>
        static coroutine_handle<details::fj_result_state<type>> get_state(fork_result<type>& fj_result) noexcept {
            return fj_result.m_state;
        }

        template<class type>
        static coroutine_handle<details::fj_result_state<type>> move_state(fork_result<type>& fj_result) noexcept {
            return std::exchange(fj_result.m_state, {});
        }
    };
}  // namespace concurrencpp::details

namespace concurrencpp {
    template<class type>
    class fork_result {

        friend details::fj_helper;

       protected:
        details::coroutine_handle<details::fj_result_state<type>> m_state;

        void throw_if_empty(const char* err_msg) const {
            if (!static_cast<bool>(m_state)) {
                throw errors::empty_result(err_msg);
            }
        }

        template<class executor_type>
        static lazy_result<type> as_root_imp(executor_type* executor, fork_result<type> fr) {
            auto result = co_await fork_join(executor, std::move(fr));
            co_return result.get();
        }

       public:
        fork_result() noexcept = default;
        fork_result(fork_result&& rhs) noexcept : m_state(std::exchange(rhs.m_state, {})) {}
        fork_result(details::coroutine_handle<details::fj_result_state<type>> state) noexcept : m_state(state) {}

        fork_result(const fork_result&) = delete;
        fork_result& operator=(const fork_result& rhs) = delete;

        ~fork_result() noexcept {
            if (static_cast<bool>(m_state)) {
                m_state.destroy();
            }
        }

        fork_result& operator=(fork_result&& rhs) noexcept {
            if (&rhs == this) {
                return *this;
            }

            if (static_cast<bool>(m_state)) {
                m_state.destroy();
            }

            m_state = std::exchange(rhs.m_state, {});
            return *this;
        }

        explicit operator bool() const noexcept {
            return static_cast<bool>(m_state);
        }

        template<class executor_type>
        lazy_result<type> as_root(executor_type* executor) {
            throw_if_empty("concurrencpp::fork_result<type>::as_root - *this is empty.");
            return as_root_imp(executor, std::move(*this));
        }
    };

    template<class type>
    class join_result final : public fork_result<type> {

       public:
        using fork_result<type>::fork_result;
        using fork_result<type>::operator=;

        type get() {
            return this->m_state.promise().get();
        }
    };
}  // namespace concurrencpp

namespace std {
    template<class type, class... arguments>
    struct coroutine_traits<::concurrencpp::fork_result<type>, arguments...> {
        using promise_type = concurrencpp::details::fj_promise<type>;
    };
}  // namespace std

namespace concurrencpp::details {
    class fj_awaitable_base : public std::suspend_always {

       protected:
        std::atomic_size_t m_count;
        std::coroutine_handle<void> m_caller_handle;

       public:
        fj_awaitable_base(size_t count) noexcept : m_count(count) {}

        void task_completed() noexcept {
            if (m_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                m_caller_handle();
            }
        }
    };

    template<class executor_type, class type, size_t n>
    class fj_awaitable : public fj_awaitable_base {

       private:
        std::array<fork_result<type>, n> m_results;
        std::array<task_state, n - 1> m_tasks;
        executor_type* m_executor;

        void throw_if_empty_result() const {
            for (const auto& result : m_results) {
                if (!static_cast<bool>(result)) [[unlikely]] {
                    throw std::runtime_error("concurrencpp::fork_join - one of the given fork_result is empty.");
                }
            }
        }

       public:
        template<class... result_types>
        fj_awaitable(executor_type* executor, result_types&&... results) :
            fj_awaitable_base(n), m_results(std::move(results)...), m_executor(executor) {
            throw_if_empty_result();
        }

        fj_awaitable(executor_type* executor, std::array<fork_result<type>, n>& results) :
            fj_awaitable_base(n), m_results(std::move(results)), m_executor(executor) {
            throw_if_empty_result();
        }

        void await_suspend(coroutine_handle<void> caller_handle) {
            m_caller_handle = caller_handle;

            for (size_t i = 0; i < m_results.size() - 1; i++) {
                auto state = fj_helper::get_state(m_results[i]);
                state.promise().fj_start(this);
                m_tasks[i].set_handle(state);
                m_executor->enqueue(task(&m_tasks[i]));
            }

            auto last = fj_helper::get_state(m_results.back());
            last.promise().fj_start(this);
            last.resume();
        }

        std::array<join_result<type>, n> await_resume() noexcept {
            std::array<join_result<type>, n> join_array;
            for (size_t i = 0; i < n; i++) {
                join_array[i] = join_result<type>(fj_helper::move_state(m_results[i]));
            }

            return std::move(join_array);
        }
    };

    template<class executor_type, class type>
    class fj_awaitable<executor_type, type, 1> : public fj_awaitable_base {

       private:
        fork_result<type> m_result;
        executor_type* m_executor;

       public:
        fj_awaitable(executor_type* executor, fork_result<type> result) noexcept :
            fj_awaitable_base(1), m_result(std::move(result)), m_executor(executor) {
            if (!static_cast<bool>(m_result)) {
                throw std::runtime_error("concurrencpp::fork_join - given fork_result is empty.");
            }
        }

        void await_suspend(coroutine_handle<void> caller_handle) noexcept {
            m_caller_handle = caller_handle;
            auto state = fj_helper::get_state(m_result);
            state.promise().fj_start(this);
            state.resume();
        }

        join_result<type> await_resume() noexcept {
            return join_result<type>(fj_helper::move_state(m_result));
        }
    };

    template<class executor_type, class type>
    class fj_vec_awaitable : public fj_awaitable_base {

       private:
        std::vector<fork_result<type>> m_results;
        std::unique_ptr<task_state[]> m_tasks;
        executor_type* m_executor;

        void throw_if_empty_result() const {
            for (const auto& result : m_results) {
                if (!static_cast<bool>(result)) [[unlikely]] {
                    throw std::runtime_error("concurrencpp::fork_join - one of the given fork_result is empty.");
                }
            }
        }

       public:
        template<std::input_iterator iterator_type>
        fj_vec_awaitable(executor_type* executor, iterator_type begin, iterator_type end) :
            fj_awaitable_base(std::distance(begin, end)), m_results(std::make_move_iterator(begin), std::make_move_iterator(end)),
            m_executor(executor) {
            throw_if_empty_result();
        }

        fj_vec_awaitable(executor_type* executor, std::vector<fork_result<type>> results) :
            fj_awaitable_base(results.size()), m_results(std::move(results)), m_executor(executor) {
            throw_if_empty_result();
        }

        bool await_suspend(coroutine_handle<void> caller_handle) {
            m_caller_handle = caller_handle;

            if (m_results.empty()) {
                return false;
            }

            if (m_results.size() > 1) {
                m_tasks = std::make_unique<task_state[]>(m_results.size() - 1);
            }

            for (size_t i = 0; i < m_results.size() - 1; i++) {
                auto state = fj_helper::get_state(m_results[i]);
                state.promise().fj_start(this);
                m_tasks[i].set_handle(state);
                m_executor->enqueue(task(&m_tasks[i]));
            }

            auto last = fj_helper::get_state(m_results.back());
            last.promise().fj_start(this);
            last.resume();
            return true;
        }

        std::vector<join_result<type>> await_resume() {
            std::vector<join_result<type>> join_array;
            join_array.reserve(m_results.size());
            for (auto& result : m_results) {
                join_array.emplace_back(fj_helper::move_state(result));
            }

            return join_array;
        }
    };
}  // namespace concurrencpp::details

namespace concurrencpp {
    template<class executor_type, class type>
    auto fork_join(executor_type* executor, fork_result<type> result) {
        if (executor == nullptr) [[unlikely]] {
            throw std::invalid_argument("null executor");
        }

        return details::fj_awaitable<executor_type, type, 1> {executor, std::move(result)};
    }

    template<class executor_type, class type, class... result_types>
    auto fork_join(executor_type* executor, fork_result<type> result, result_types&&... results) {
        static_assert((std::is_same_v<fork_result<type>, std::decay_t<result_types>> && ...),
                      "concurrencpp::fork_join - all results must have the same type fork_result<type>");

        if (executor == nullptr) [[unlikely]] {
            throw std::invalid_argument("null executor");
        }

        return details::fj_awaitable<executor_type, type, sizeof...(results) + 1> {executor,
                                                                                   std::move(result),
                                                                                   std::forward<result_types>(results)...};
    }

    template<class executor_type, class type, size_t n>
    auto fork_join(executor_type* executor, std::array<fork_result<type>, n>& results) {
        if (executor == nullptr) [[unlikely]] {
            throw std::invalid_argument("null executor");
        }

        return details::fj_awaitable<executor_type, type, n> {executor, results};
    }

    template<class executor_type, class type>
    auto fork_join(executor_type* executor, std::vector<fork_result<type>> results) {
        if (executor == nullptr) [[unlikely]] {
            throw std::invalid_argument("null executor");
        }

        return details::fj_vec_awaitable<executor_type, type> {executor, std::move(results)};
    }

    template<class executor_type, std::input_iterator iterator_type>
    auto fork_join(executor_type* executor, iterator_type begin, iterator_type end) {
        if (executor == nullptr) [[unlikely]] {
            throw std::invalid_argument("null executor");
        }

        using type = decltype(*begin);
        return details::fj_vec_awaitable<executor_type, type> {executor, begin, end};
    }
}  // namespace concurrencpp

#endif