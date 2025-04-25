#include "concurrencpp/results/fj_result.h"

using concurrencpp::details::fj_result_state_base;
using concurrencpp::details::fj_awaitable_base;

void fj_result_state_base::fj_start(fj_awaitable_base* parent_awaitable) noexcept {
    assert(m_parent_awaitable == nullptr);
    assert(parent_awaitable != nullptr);
    m_parent_awaitable = parent_awaitable;
}

void fj_result_state_base::fj_end() const noexcept {
    assert(m_parent_awaitable != nullptr);
    m_parent_awaitable->task_completed();
}

std::suspend_always fj_result_state_base::initial_suspend() const noexcept {
    return {};
}

concurrencpp::details::fj_final_awaiter fj_result_state_base::final_suspend() const noexcept {
    return {};
}