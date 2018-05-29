
#include "eventloop.h"
#include <chrono>
#include <vector>
#include <map>
#include <algorithm>
#include <string.h>

namespace lynx {
using std::chrono::system_clock;
using std::chrono::duration;
using std::chrono::duration_cast;

class Poller
{
public:
    enum {
        in  = 1,
        out = 2,
    };

    virtual ~Poller() {}
    virtual bool resize(size_t size)   = 0;
    virtual bool add_event(int fd, int mask) = 0;
    virtual void del_event(int fd, int mask) = 0;
    virtual void poll(struct timeval *) = 0;
};

struct TimerHandle {
    size_t                   counts;
    long long                interval;
    system_clock::time_point when;
    EventLoop::TimerFn       proc;
};

struct EventLoop::Impl {
    int                     maxfd = -1;
    long long               next_timer_id = 0;
    bool                    stop = false;
    std::shared_ptr<Poller> poller = nullptr;
    std::map<int, IoFn>     read_fns;
    std::map<int, IoFn>     write_fns;
    std::map<long long, TimerHandle> timer_fns;
};

class SelectPoller : public Poller
{
public:
    SelectPoller(const int& maxfd, const std::map<int, EventLoop::IoFn>& read_fns,
                const std::map<int, EventLoop::IoFn>& write_fns)
        : maxfd_(maxfd), read_fns_(read_fns), write_fns_(write_fns)
    {
        FD_ZERO(&rfds_);
        FD_ZERO(&wfds_);
    }

    ~SelectPoller() {}

    bool resize(size_t size) override {
        return size < FD_SETSIZE;
    }

    bool add_event(int fd, int mode) override {
        if (mode & Poller::in)  FD_SET(fd, &rfds_);
        if (mode & Poller::out) FD_SET(fd, &wfds_);
        return true;
    }

    void del_event(int fd, int mode) override {
        if (mode & Poller::in)  FD_CLR(fd, &rfds_);
        if (mode & Poller::out) FD_CLR(fd, &wfds_);
    }

    void poll(struct timeval *tvp) override {
        fd_set rfds, wfds;
        memcpy(&rfds, &rfds_, sizeof(fd_set));
        memcpy(&wfds, &wfds_, sizeof(fd_set));

        int retval = select(maxfd_+1, &rfds, &wfds, nullptr, tvp);
        if (retval > 0) {
            // find all read events
            for (const auto &kv : read_fns_) {
                if (FD_ISSET(kv.first, &rfds))
                    kv.second();
            }

            // find all write events
            for (const auto &kv : write_fns_) {
                if (FD_ISSET(kv.first, &wfds))
                    kv.second();
            }
        }
    }
private:
    fd_set rfds_, wfds_;
    const int&                             maxfd_;
    const std::map<int, EventLoop::IoFn>&  read_fns_;
    const std::map<int, EventLoop::IoFn>&  write_fns_;
};


/* * * * * * * * * * * * * * * * * * * *
 * EventLoop 
 */
EventLoop::EventLoop() : impl_(new Impl)
{
    if (impl_->poller == nullptr) {
        auto select_poller = new SelectPoller(impl_->maxfd, 
                                            impl_->read_fns, 
                                            impl_->write_fns);
        impl_->poller = std::shared_ptr<Poller>(select_poller);
    }
}

EventLoop::~EventLoop() { }
void EventLoop::start() { while(!impl_->stop) process_evts(); }
void EventLoop::stop()  { impl_->stop = true; }

void EventLoop::process_evts()
{
    auto &time_evts = impl_->timer_fns;

    // sleep until events coming 
    struct timeval tv, *tvp = nullptr;
    auto cmp = [](const std::pair<long long, TimerHandle>& a, 
                  const std::pair<long long, TimerHandle>& b) {
        return a.second.when < b.second.when;
    };
    auto shortest = std::min_element(time_evts.begin(), 
                                     time_evts.end(),
                                     cmp);

    if (shortest != time_evts.end()) {
        //shortest->when
        auto now = system_clock::now();
        auto ms = duration_cast<std::chrono::milliseconds>(
                     shortest->second.when - now
                );
        int ms_count = ms.count();
        if (ms_count < 0) ms_count = 0;
        tv.tv_sec  = ms.count()/1000;
        tv.tv_usec = (ms.count()%1000)*1000;
        tvp = &tv;
    }

    // poll io event
    impl_->poller->poll(tvp);

    // process timer events
    auto now = system_clock::now();
    std::vector<long long> to_remove;
    for (auto &te: time_evts) {
        auto &th = te.second;
        if (th.when < now) {
            th.when += duration<int, std::milli>(th.interval);
            th.proc(te.first);
            if (th.counts != lynx::MATH_HUGE) th.counts--;
            if (th.counts == 0) to_remove.push_back(te.first);
        }
    }
    for (auto id: to_remove) del_timer_id(id);
}

void EventLoop::async_read_impl(int fd, const IoFn& fn) 
{
    if (impl_->poller->add_event(fd, Poller::in)) return;
    impl_->read_fns[fd] = fn;
}

void EventLoop::async_write_impl(int fd, const IoFn& fn)
{
    if (impl_->poller->add_event(fd, Poller::out)) return;
    impl_->write_fns[fd] = fn;
}

long long EventLoop::every_impl(long long ms, size_t times, const TimerFn& fn)
{
    auto id = impl_->next_timer_id++; 
    auto w  = system_clock::now() + duration<int, std::milli>(ms);
    TimerHandle th { times, ms, w, fn };

    impl_->timer_fns[id] = th; 
    return id;
}

void EventLoop::del_async_read_fn(int fd) 
{
    auto iter = impl_->read_fns.find(fd);
    if (iter != impl_->read_fns.end())
        impl_->read_fns.erase(iter);
}

void EventLoop::del_async_write_fn(int fd) 
{
    auto iter = impl_->write_fns.find(fd);
    if (iter != impl_->write_fns.end())
        impl_->write_fns.erase(iter);
}

void EventLoop::del_timer_id(long long id) 
{ 
    auto &time_evts = impl_->timer_fns;
    auto iter = time_evts.find(id);
    if (iter != time_evts.end()) time_evts.erase(iter);
}

} // end namespace lynx

