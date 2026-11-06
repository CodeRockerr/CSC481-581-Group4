#include "EventManager.h"
#include "Timeline.h"
#include <stdexcept>
#include <utility>

EventManager::EventManager(Timeline &timeline, uint32_t ownerId)
    : EventManager([&timeline]() { return static_cast<EventTime>(timeline.getSeconds() * 1'000'000); }, ownerId)
{
}

EventManager::EventManager(Clock clock, uint32_t ownerId)
    : clock(std::move(clock)), ownerId(ownerId)
{
    if (!this->clock)
        throw std::invalid_argument("EventManager requires a clock");
}

bool EventManager::Later::operator()(const QueuedEvent &a, const QueuedEvent &b) const
{
    if (a.event.timestamp != b.event.timestamp)
        return a.event.timestamp > b.event.timestamp;
    if (a.event.priority != b.event.priority)
        return a.event.priority < b.event.priority;
    return a.sequence > b.sequence;
}

EventManager::Subscription EventManager::addListener(std::optional<EventType> type, Handler handler)
{
    if (!handler)
        throw std::invalid_argument("Event handler cannot be empty");
    std::lock_guard<std::mutex> lock(mutex);
    const auto subscription = nextSubscription++;
    listeners.emplace(subscription, Listener{type, std::move(handler)});
    return subscription;
}

EventManager::Subscription EventManager::subscribe(EventType type, Handler handler)
{
    if (type == 0)
        throw std::invalid_argument("Event type zero is reserved");
    return addListener(type, std::move(handler));
}

EventManager::Subscription EventManager::subscribeAll(Handler handler)
{
    return addListener(std::nullopt, std::move(handler));
}

bool EventManager::unsubscribe(Subscription subscription)
{
    std::lock_guard<std::mutex> lock(mutex);
    return listeners.erase(subscription) != 0;
}

EventId EventManager::raise(Event event)
{
    if (event.type == 0)
        throw std::invalid_argument("Event type zero is reserved");
    if (event.timestamp < 0)
        event.timestamp = now();
    std::lock_guard<std::mutex> lock(mutex);
    if (playback && event.origin != EventOrigin::Replay && !isReplayControl(event.type) &&
        (event.type == EventTypes::Input || event.origin == EventOrigin::Network))
        return {};
    if ((playback || (replayHandler && handlerThread == std::this_thread::get_id())) &&
        event.origin == EventOrigin::Local && !isReplayControl(event.type))
    {
        event.origin = EventOrigin::Replay;
        event.shareable = false;
    }
    if (isReplayControl(event.type))
        event.shareable = false;
    if (event.id.sequence == 0)
        event.id = {ownerId, nextId++};
    const auto id = event.id;
    queue.push({std::move(event), nextSequence++});
    return id;
}

std::size_t EventManager::dispatch(std::size_t budget)
{
    if (dispatching.exchange(true))
        throw std::logic_error("Event dispatch must have one non-recursive consumer");
    try
    {
        const auto handled = dispatchPending(budget);
        dispatching = false;
        return handled;
    }
    catch (...)
    {
        dispatching = false;
        throw;
    }
}

std::size_t EventManager::dispatchPending(std::size_t budget)
{
    std::size_t handled = 0;
    while (handled < budget)
    {
        const auto time = now();
        Event event;
        std::vector<Subscription> subscriptions;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (queue.empty() || queue.top().event.timestamp > time)
                break;
            event = queue.top().event;
            queue.pop();
            if (playback && event.origin != EventOrigin::Replay && !isReplayControl(event.type) &&
                (event.type == EventTypes::Input || event.origin == EventOrigin::Network))
            {
                ++handled;
                continue;
            }
            for (const auto &entry : listeners)
                if (!entry.second.type || *entry.second.type == event.type)
                    subscriptions.push_back(entry.first);
            replayHandler = event.origin == EventOrigin::Replay;
            handlerThread = std::this_thread::get_id();
        }
        try
        {
            for (auto subscription : subscriptions)
            {
                Handler handler;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    auto found = listeners.find(subscription);
                    if (found != listeners.end())
                        handler = found->second.handler;
                }
                if (handler)
                    handler(event);
            }
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mutex);
            replayHandler = false;
            throw;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            replayHandler = false;
        }
        ++handled;
    }
    return handled;
}

std::size_t EventManager::pending() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return queue.size();
}

std::vector<Event> EventManager::getPendingEvents() const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto pending = queue;
    std::vector<Event> result;
    while (!pending.empty())
    {
        result.push_back(pending.top().event);
        pending.pop();
    }
    return result;
}

void EventManager::clear()
{
    std::lock_guard<std::mutex> lock(mutex);
    queue = {};
}

void EventManager::setOwnerId(uint32_t id)
{
    std::lock_guard<std::mutex> lock(mutex);
    ownerId = id;
}

EventTime EventManager::now() const
{
    const auto time = clock();
    std::lock_guard<std::mutex> lock(mutex);
    return manualTime ? *manualTime : time + timeOffset;
}

void EventManager::setTime(EventTime time)
{
    if (time < 0)
        throw std::invalid_argument("Event time cannot be negative");
    std::lock_guard<std::mutex> lock(mutex);
    manualTime = time;
}

void EventManager::resumeTime()
{
    const auto time = clock();
    std::lock_guard<std::mutex> lock(mutex);
    if (manualTime)
        timeOffset = *manualTime - time;
    manualTime.reset();
}

void EventManager::setPlaybackMode(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex);
    playback = enabled;
}

bool EventManager::isPlaybackMode() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return playback;
}
