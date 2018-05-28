
#pragma once

#include <memory>
#include <functional>

// ** forward declaration
struct timeval;

namespace lynx {

enum IoMode {
    in    = 1,
    out   = 2,
};

enum Events {
    file = 1,
    time = 2,
    all  = (file | time),
};

class Poller
{
public:
    virtual ~Poller() {}
    virtual bool Resize(size_t size)   = 0;
    virtual bool AddEvent(int fd, IoMode mode) = 0;
    virtual void DelEvent(int fd, IoMode mode) = 0;
    virtual void Poll(struct timeval *, const std::function<void (int, IoMode)>&) = 0;
};

class EventLoop final
{
public:
    EventLoop();
    ~EventLoop();
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(const EventLoop&) = delete;

    void Start();
    void ProcessEvents();
    void Stop();

    // io event
    template<class Func, class... Args>
    bool CreateIOEvent(int fd, IoMode mask, Func func, Args... args) {
        auto f = std::bind(std::forward<Func>(func), fd,
                           std::forward<Args>(args)...);
        return createIOEventImpl(fd, mask, f);
    }
    void DeleteFileEvent(int fd, IoMode mode);

    // timer
    template<class Func, class... Args>
    long long CreateTimeEvent(long long ms, Func func, Args... args) {
        auto f = std::bind(std::forward<Func>(func),
                           std::forward<Args>(args)...);
        return createTimeEventImpl(ms, f);
    }
    void DeleteTimeEvent(long long id);

private:
    bool createIOEventImpl(int fd, IoMode mask, const std::function<void ()>& f);
    long long createTimeEventImpl(long long ms, const std::function<int ()>& f);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} /* end namespace lynx */
