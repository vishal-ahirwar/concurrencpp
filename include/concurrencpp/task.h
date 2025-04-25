#ifndef CONCURRECPP_TASK_H
#define CONCURRECPP_TASK_H

#include "concurrencpp/errors.h"
#include "concurrencpp/results/constants.h"
#include "concurrencpp/coroutines/coroutine.h"

#include <memory>

#include <cassert>

namespace concurrencpp {
    class CRCPP_API task_state : public details::suspend_always {

       private:
        enum class status { idle, interrupted, started };

       private:
        details::coroutine_handle<void> m_caller_handle;
        status m_status;

       public:
        task_state() noexcept;

        task_state(const task_state&) = delete;
        task_state(task_state&&) = delete;

        ~task_state() noexcept;

        void set_handle(details::coroutine_handle<void> caller_handle) noexcept;
 
        void call() noexcept;
        void interrupt() noexcept;

        void await_resume() const;
    };

    class task {

       private:
        task_state* m_state;

       public:
        task() noexcept;
        task(task_state* state) noexcept;
        task(task&& rhs) noexcept;
        ~task() noexcept;

        task& operator=(task&& rhs) noexcept;
        task& operator=(const task& rhs) = delete;

        void operator()() noexcept;

        bool empty() const noexcept;
    };

}  // namespace concurrencpp

#endif