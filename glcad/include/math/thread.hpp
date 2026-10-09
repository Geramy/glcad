// std::thread's 512 KB macOS/iOS stack overflowed on deep JSON parsing.
#pragma once

#include <functional>
#include <memory>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

namespace gcm {

class BigStackThread {
public:
    static constexpr std::size_t kStackBytes = 16u * 1024u * 1024u;

    BigStackThread() = default;
    explicit BigStackThread(std::function<void()> fn) { start(std::move(fn)); }

    BigStackThread(BigStackThread&& o) noexcept { adopt(o); }
    BigStackThread& operator=(BigStackThread&& o) noexcept {
        if (this != &o) {
            if (joinable()) join();
            adopt(o);
        }
        return *this;
    }
    BigStackThread(const BigStackThread&) = delete;
    BigStackThread& operator=(const BigStackThread&) = delete;

    ~BigStackThread() {
        if (joinable()) join();       // gentler than std::thread's terminate
    }

    bool joinable() const noexcept { return live_; }

    void join() {
        if (!live_) return;
#if defined(_WIN32)
        WaitForSingleObject(handle_, INFINITE);
        CloseHandle(handle_);
        handle_ = nullptr;
#else
        pthread_join(thread_, nullptr);
#endif
        live_ = false;
    }

private:
    struct Box {
        std::function<void()> fn;
    };

    void adopt(BigStackThread& o) noexcept {
#if defined(_WIN32)
        handle_ = o.handle_;
        o.handle_ = nullptr;
#else
        thread_ = o.thread_;
#endif
        live_ = o.live_;
        o.live_ = false;
    }

    void start(std::function<void()> fn) {
        auto box = std::make_unique<Box>(Box{std::move(fn)});
#if defined(_WIN32)
        const auto entry = [](void* p) -> unsigned {
            std::unique_ptr<Box> b(static_cast<Box*>(p));
            b->fn();
            return 0u;
        };
        handle_ = reinterpret_cast<HANDLE>(_beginthreadex(
            nullptr, unsigned(kStackBytes),
            static_cast<unsigned(__stdcall*)(void*)>(entry), box.get(),
            STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
        if (handle_) {
            box.release();
            live_ = true;
        }
#else
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, kStackBytes);
        const int rc = pthread_create(
            &thread_, &attr,
            [](void* p) -> void* {
                std::unique_ptr<Box> b(static_cast<Box*>(p));
                b->fn();
                return nullptr;
            },
            box.get());
        pthread_attr_destroy(&attr);
        if (rc == 0) {
            box.release();
            live_ = true;
        }
#endif
    }

#if defined(_WIN32)
    HANDLE handle_{nullptr};
#else
    pthread_t thread_{};
#endif
    bool live_{false};
};

}
