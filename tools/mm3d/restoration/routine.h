// SPDX-License-Identifier: GPL-2.0-or-later
// Nested source-port routines over the runtime's existing guest-call adapter.
#pragma once
#include "mm3d_guest_calls.h"
#include "rst_guest.h"
#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace Mm3dRestoration {
struct Routine {
    struct promise_type {
        u32 result=0;
        std::exception_ptr failure;
        std::coroutine_handle<> parent=std::noop_coroutine();
        Routine get_return_object() { return Routine{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        struct Final {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept { return h.promise().parent; }
            void await_resume() noexcept {}
        };
        Final final_suspend() noexcept { return {}; }
        void return_value(u32 value) noexcept { result=value; }
        void unhandled_exception() noexcept { failure=std::current_exception(); }
    };
    std::coroutine_handle<promise_type> handle;
    explicit Routine(std::coroutine_handle<promise_type> h):handle(h) {}
    Routine(Routine&& r) noexcept:handle(std::exchange(r.handle,{})) {}
    Routine(const Routine&)=delete;
    ~Routine() { if(handle)handle.destroy(); }
    bool Done() const { return handle.done(); }
    u32 Result() const { if(handle.promise().failure)std::rethrow_exception(handle.promise().failure);return handle.promise().result; }
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) noexcept {handle.promise().parent=parent;return handle;}
    u32 await_resume() const { return Result(); }
};

struct CallMachine {
    using Arguments=Oot3dNativeGame::Mm3dGuestCallArguments;
    using Result=Oot3dNativeGame::Mm3dGuestCallResult;
    struct Request {u32 entry;Arguments args;};
    std::optional<Request> pending;
    std::coroutine_handle<> leaf;
    Result result;
    struct Awaiter {
        CallMachine& machine;Request request;
        bool await_ready() noexcept {return false;}
        void await_suspend(std::coroutine_handle<> h) noexcept {machine.leaf=h;machine.pending=std::move(request);}
        Result await_resume() noexcept {return machine.result;}
    };
    Awaiter Call(u32 entry,Arguments args={}) {return {*this,{entry,std::move(args)}};}
};
} // namespace Mm3dRestoration
