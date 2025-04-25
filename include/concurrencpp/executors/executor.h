#ifndef CONCURRENCPP_EXECUTOR_H
#define CONCURRENCPP_EXECUTOR_H

#include "concurrencpp/task.h"
#include "concurrencpp/results/result.h"

#include <span>
#include <vector>
#include <string>
#include <string_view>

namespace concurrencpp::details {
    [[noreturn]] CRCPP_API void throw_runtime_shutdown_exception(std::string_view executor_name);
    CRCPP_API std::string make_executor_worker_name(std::string_view executor_name);
}  // namespace concurrencpp::details

namespace concurrencpp {
    class CRCPP_API executor {

       private:
        template<class executor_type, class callable_type, class... argument_types>
        static null_result post_bridge(executor_tag, executor_type&, callable_type callable, argument_types... arguments) {
            co_return callable(arguments...);
        }

        template<class return_type, class executor_type, class callable_type, class... argument_types>
        static result<return_type> submit_bridge(executor_tag, executor_type&, callable_type callable, argument_types... arguments) {
            co_return callable(arguments...);
        }

       protected:
        template<class executor_type, class callable_type, class... argument_types>
        null_result do_post(callable_type&& callable, argument_types&&... arguments) {
            static_assert(std::is_invocable_v<callable_type, argument_types...>,
                          "concurrencpp::executor::post - <<callable_type>> is not invokable with <<argument_types...>>");

            if (shutdown_requested()) {
                details::throw_runtime_shutdown_exception(name);
            }

            return post_bridge({},
                               *static_cast<executor_type*>(this),
                               std::forward<callable_type>(callable),
                               std::forward<argument_types>(arguments)...);
        }

        template<class executor_type, class callable_type, class... argument_types>
        auto do_submit(callable_type&& callable, argument_types&&... arguments) {
            static_assert(std::is_invocable_v<callable_type, argument_types...>,
                          "concurrencpp::executor::submit - <<callable_type>> is not invokable with <<argument_types...>>");

            if (shutdown_requested()) {
                details::throw_runtime_shutdown_exception(name);
            }

            using return_type = typename std::invoke_result_t<callable_type, argument_types...>;
            return submit_bridge<return_type>({},
                                              *static_cast<executor_type*>(this),
                                              std::forward<callable_type>(callable),
                                              std::forward<argument_types>(arguments)...);
        }

       public:
        executor(std::string_view name) : name(name) {}

        virtual ~executor() noexcept = default;

        const std::string name;

        virtual void enqueue(task task) = 0;

        virtual int max_concurrency_level() const noexcept = 0;

        virtual bool shutdown_requested() const = 0;
        virtual void shutdown() = 0;

        template<class callable_type, class... argument_types>
        null_result post(callable_type&& callable, argument_types&&... arguments) {
            return do_post<executor>(std::forward<callable_type>(callable), std::forward<argument_types>(arguments)...);
        }

        template<class callable_type, class... argument_types>
        auto submit(callable_type&& callable, argument_types&&... arguments) {
            return do_submit<executor>(std::forward<callable_type>(callable), std::forward<argument_types>(arguments)...);
        }
    };
}  // namespace concurrencpp

#endif
