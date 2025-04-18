#ifndef CONCURRENCPP_DERIVABLE_EXECUTOR_H
#define CONCURRENCPP_DERIVABLE_EXECUTOR_H

#include "concurrencpp/utils/bind.h"
#include "concurrencpp/executors/executor.h"

namespace concurrencpp {
    template<class concrete_executor_type>
    struct CRCPP_API derivable_executor : public executor {

        derivable_executor(std::string_view name) : executor(name) {}

        template<class callable_type, class... argument_types>
        null_result post(callable_type&& callable, argument_types&&... arguments) {
            return do_post<concrete_executor_type>(std::forward<callable_type>(callable), std::forward<argument_types>(arguments)...);
        }

        template<class callable_type, class... argument_types>
        auto submit(callable_type&& callable, argument_types&&... arguments) {
            return do_submit<concrete_executor_type>(std::forward<callable_type>(callable),
                                                     std::forward<argument_types>(arguments)...);
        }
    };
}  // namespace concurrencpp

#endif
