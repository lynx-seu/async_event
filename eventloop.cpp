
#include <vector>
#include <map>
#include <chrono>
#include <algorithm>
#include <string.h>
#include "eventloop.h"

namespace lynx {

using std::chrono::system_clock;
using std::chrono::duration;

struct IoEvent {
    IoMode mode;
    std::function<void()> i_proc; 
    std::function<void()> o_proc; 
};

struct TimeEvent {
    long long                 id;     //time event identifier
    system_clock::time_point  when;
    std::function<int ()>    time_proc;
};

// impl of eventloop
struct EventLoop::Impl {
    std::map<int, IoEvent> io_events;
    std::vector<TimeEvent> timer_events;
    int         maxfd               = -1;
    long long   next_timer_id       = 0;
    bool        stop                = false;
    std::shared_ptr<Poller> poller  = nullptr;
};


namespace internal {
} // end of namespace

class SelectPoller : public Poller
{
public:
    SelectPoller(const int& maxfd, const std::map<int, IoEvent>& evts)
        : maxfd_(maxfd), io_events_(evts) 
    {
        FD_ZERO(&rfds_);
        FD_ZERO(&wfds_);
    }

    ~SelectPoller() {

    }

    bool Resize(size_t size) override {
        return size < FD_SETSIZE;
    }

    bool AddEvent(int fd, IoMode mode) override {
        if (mode & IoMode::in)  FD_SET(fd, &rfds_);
        if (mode & IoMode::out) FD_SET(fd, &wfds_);
        return true;
    }

    void DelEvent(int fd, IoMode mode) override {
        if (mode & IoMode::in)  FD_CLR(fd, &rfds_);
        if (mode & IoMode::out) FD_CLR(fd, &wfds_);
    }

    void Poll(struct timeval *tvp, 
            const std::function<void (int, IoMode)>& process) override {
        fd_set rfds, wfds;
        memcpy(&rfds, &rfds_, sizeof(fd_set));
        memcpy(&wfds, &wfds_, sizeof(fd_set));

        int retval = select(maxfd_+1, &rfds, &wfds, nullptr, tvp);
        if (retval > 0) {
            std::vector<std::pair<int, IoMode>> toprocess;
            // find all io events
            for (const auto &kv : io_events_) {
                int mode = 0;
                int fd = kv.first;
                const IoEvent& io = kv.second;

                if (io.mode & IoMode::in && FD_ISSET(fd, &rfds))
                    mode = IoMode::in;
                if (io.mode & IoMode::out && FD_ISSET(fd, &wfds))
                    mode = IoMode::out;

                if (mode != 0) 
                    toprocess.push_back(std::make_pair(fd, (IoMode)mode));
            }
            
            // process all events
            for (const auto& v: toprocess) {
                process(v.first, v.second);
            }
        }
    }
private:
    fd_set rfds_, wfds_;
    const int &maxfd_;
    const std::map<int, IoEvent>& io_events_;
};

EventLoop::EventLoop()
    :impl_(new Impl)
{
    if (impl_->poller == nullptr) {
        auto selector = new SelectPoller(impl_->maxfd, impl_->io_events);
        impl_->poller = std::shared_ptr<Poller>(selector);
    }
}

EventLoop::~EventLoop() {}

void EventLoop::Start()
{
    while (!impl_->stop) {
        ProcessEvents();
    }
}

void EventLoop::Stop() 
{
    impl_->stop = true;
}

void EventLoop::ProcessEvents()
{
    auto &timer_events = impl_->timer_events;

    if (impl_->maxfd != -1) {
        struct timeval *tvp = nullptr;
        struct timeval tv;

        auto f = [](const TimeEvent &a, const TimeEvent& b) -> bool {
            return a.when < b.when;
        };
        auto shortest = std::min_element(timer_events.begin(),
                                         timer_events.end(), f);

        if (shortest != timer_events.end()) {
            //shortest->when
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                        shortest->when.time_since_epoch()
                    );
            tv.tv_sec  = ms.count()/1000;
            tv.tv_usec = (ms.count()%1000)*1000;
            tvp = &tv;
        }
        // poll io event
        impl_->poller->Poll(tvp, [&](int fd, IoMode mode) {
                const auto iter = impl_->io_events.find(fd);
                if (iter == impl_->io_events.end()) return;
                const auto& evt = iter->second;

                bool bin = false;
                if (evt.mode & mode & IoMode::in) {
                    bin = true;
                    evt.i_proc();
                }
                if (evt.mode & mode & IoMode::out) {
                    //if(!bin || evt.i_proc != evt.o_proc)
                        evt.o_proc();
                }
        });
    }

    // process time event
    auto now = system_clock::now();
    for (auto &te: timer_events) {
        if (te.when < now) {
            int id     = te.id;
            int retval = te.time_proc();

            if (retval >= 0) {
                te.when += duration<int, std::milli>(retval);
            } else {
                DeleteTimeEvent(id);
            }
        }
    }
}

bool EventLoop::createIOEventImpl(int fd, IoMode mask, 
                    const std::function<void ()>& f)
{
    if (!impl_->poller->AddEvent(fd, mask))
        return false;

    auto &evts = impl_->io_events;
    if (evts.end() == evts.find(fd)) {
        IoEvent io {
            .mode = mask,
        };

        if (mask & IoMode::in)  io.i_proc = f;
        if (mask & IoMode::out) io.o_proc = f;
        evts.insert(std::make_pair(fd, io));
    } else {
        auto &io = evts.at(fd);
        io.mode = (IoMode)(io.mode | mask);
        if (mask & IoMode::in)  io.i_proc = f;
        if (mask & IoMode::out) io.o_proc = f;
    }

    if (fd > impl_->maxfd) impl_->maxfd = fd;
    return true;
}

void EventLoop::DeleteFileEvent(int fd, IoMode mask)
{
    auto &evts = impl_->io_events;
    auto iter = evts.find(fd);
    if (iter == evts.end()) return;

    auto &io = iter->second;
    impl_->poller->DelEvent(fd, mask);
    io.mode = (IoMode)(io.mode & (~mask));
    if ((int)io.mode == 0) {
        evts.erase(iter);

        // update max fd
        if (fd == impl_->maxfd) {
            auto f = [](const std::pair<int, IoEvent>& a,
                        const std::pair<int, IoEvent>& b) {
                return a.first < b.first;
            };
            auto iter = std::max_element(evts.begin(), evts.end(), f);
            if (iter != evts.end())
                impl_->maxfd = iter->first;
        }
    }
}

void EventLoop::DeleteTimeEvent(long long id)
{
    auto &tes = impl_->timer_events;
    auto comp = [id](const TimeEvent& te) -> bool {
                return te.id == id;
    };
    auto iter = std::find_if(tes.begin(), tes.end(), comp);
    if (iter != tes.end()) {
        tes.erase(iter);
    }
}

long long EventLoop::createTimeEventImpl(long long ms,
                    const std::function<int ()>& f) 
{
    long long id = impl_->next_timer_id++;
    auto w = system_clock::now() + duration<int, std::milli>(ms);

    TimeEvent te {
        .id         = id,
        .when       = w,
        .time_proc  = f,
    };
    impl_->timer_events.push_back(te);

    return id;
}


} // end of namespace lynx
