#include "concurrencpp/task.h"

#include <utility>

#include <cassert>

using concurrencpp::task_state;
using concurrencpp::task;

/*
 * task_state
 */

task_state::task_state() noexcept : m_status(status::idle) {}

task_state ::~task_state() noexcept {
    assert(m_caller_handle ? (m_status != status::idle) : true);
}

void task_state::set_handle(details::coroutine_handle<void> caller_handle) noexcept {
    assert(static_cast<bool>(caller_handle));
    assert(!caller_handle.done());
    m_caller_handle = caller_handle;
}

void task_state::call() noexcept {
    assert(m_status == status::idle);
    m_status = status::started;
    m_caller_handle();
}

void task_state::interrupt() noexcept {
    assert((m_status == status::idle) || (m_status == status::interrupted) || (m_status == status::started));
    if (m_status == status::idle) {
        m_status = status::interrupted;
        m_caller_handle();
    }
}

void task_state::await_resume() const {
    if (m_status == status::interrupted) {
        throw errors::broken_task(details::consts::k_broken_task_exception_error_msg);
    }
}

task::task() noexcept : m_state(nullptr) {}

task::task(task_state* state) noexcept : m_state(state) {}

task::task(task&& rhs) noexcept : m_state(std::exchange(rhs.m_state, nullptr)) {}

task::~task() noexcept {
    if (m_state != nullptr) {
        m_state->interrupt();
        m_state = nullptr;
    }
}

task& concurrencpp::task::operator=(task&& rhs) noexcept {
    if (m_state != nullptr) {
        m_state->interrupt();
        m_state = nullptr;
    }

    m_state = std::exchange(rhs.m_state, nullptr);
    return *this;
}

void task::operator()() noexcept {
    if (m_state != nullptr) {
        m_state->call();
        m_state = nullptr;
    }
}

bool concurrencpp::task::empty() const noexcept {
    return m_state == nullptr;
}
