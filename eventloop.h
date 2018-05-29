
#pragma once

#include <functional>
#include <memory>
#include <climits>

namespace lynx {

// simulate infinity value
enum {
    MATH_HUGE = UINT_MAX,
};

// async event loop && timer
class EventLoop final {
public:
    using IoFn      = std::function<void ()>;
    using TimerFn   = std::function<void (long long)>;

    EventLoop();
    ~EventLoop();

    void start();
    void stop();
    void process_evts();

    template<class F, class... Args>
    inline void async_read(int fd, F&& f, Args&&... args) {
        auto io_fn = std::bind(std::forward<F>(f), fd,
                            std::forward<Args>(args)...);
        async_read_impl(fd, io_fn);
    }

    template<class F, class... Args>
    inline void async_write(int fd, F&& f, Args&&... args) {
        auto io_fn = std::bind(std::forward<F>(f), fd,
                            std::forward<Args>(args)...);
        async_write_impl(fd, io_fn);
    }

    template<class F, class... Args>
    inline long long every(long long ms, size_t times, F&& f, Args&&... args) {
        TimerFn timer_fn = std::bind(std::forward<F>(f), std::placeholders::_1,
                                std::forward<Args>(args)...);
        return every_impl(ms, times, timer_fn);
    }

    template<class F, class... Args>
    inline long long after(long long ms, F&& f, Args&&... args) {
        return every(ms, 1, std::forward<F>(f), std::forward<Args>(args)...);
    }

    void del_async_read_fn(int fd);
    void del_async_write_fn(int fd);
    void del_timer_id(long long id);
private:
    void async_read_impl(int fd, const IoFn& fn);
    void async_write_impl(int fd, const IoFn& fn);
    long long every_impl(long long ms, size_t times, const TimerFn& fn);

private:
    // follow: [pimpl idiom](https://docs.microsoft.com/en-us/cpp/cpp/pimpl-for-compile-time-encapsulation-modern-cpp)
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // end namespace lynx

