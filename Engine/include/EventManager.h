#pragma once
#include "Event.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

class Timeline;

class EventManager
{
public:
    using Clock = std::function<EventTime()>;
    using Handler = std::function<void(const Event &)>;
    using Subscription = uint64_t;
    static constexpr std::size_t DefaultBudget = 1024;

    explicit EventManager(Timeline &timeline, uint32_t ownerId = 0);
    explicit EventManager(Clock clock, uint32_t ownerId = 0);

    Subscription subscribe(EventType type, Handler handler);
    Subscription subscribeAll(Handler handler);
    bool unsubscribe(Subscription subscription);
    EventId raise(Event event);
    std::size_t dispatch(std::size_t budget = DefaultBudget);
    std::size_t pending() const;
    std::vector<Event> getPendingEvents() const;
    void clear();
    void setOwnerId(uint32_t ownerId);
    EventTime now() const;
    void setTime(EventTime time);
    void resumeTime();
    void setPlaybackMode(bool enabled);
    bool isPlaybackMode() const;

private:
    struct Listener
    {
        std::optional<EventType> type;
        Handler handler;
    };

    struct QueuedEvent
    {
        Event event;
        uint64_t sequence;
    };

    struct Later
    {
        bool operator()(const QueuedEvent &a, const QueuedEvent &b) const;
    };

    Subscription addListener(std::optional<EventType> type, Handler handler);
    std::size_t dispatchPending(std::size_t budget);

    Clock clock;
    mutable std::mutex mutex;
    std::atomic<bool> dispatching{false};
    std::priority_queue<QueuedEvent, std::vector<QueuedEvent>, Later> queue;
    std::map<Subscription, Listener> listeners;
    uint32_t ownerId;
    uint64_t nextId = 1;
    uint64_t nextSequence = 1;
    Subscription nextSubscription = 1;
    std::optional<EventTime> manualTime;
    EventTime timeOffset = 0;
    bool playback = false;
    bool replayHandler = false;
    std::thread::id handlerThread;
};
