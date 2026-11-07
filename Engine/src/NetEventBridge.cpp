#include "NetEventBridge.h"

NetEventBridge::NetEventBridge(EventManager &eventManager, NetworkClient &networkClient)
    : events(eventManager), client(networkClient)
{
    const uint32_t self = client.getClientId();
    events.setOwnerId(self);

    toWire = [](const EventObject &object)
    {
        return object;
    };
    fromWire = toWire;

    subscription = events.subscribeAll([this](const Event &event)
                                       {
        if (!event.shareable || event.origin != EventOrigin::Local || isReplayControl(event.type))
        {
            return;
        }
        // An offline reference is a local GameObject id; it means nothing to other machines.
        if (EventObjects::isOffline(event.source) || EventObjects::isOffline(event.target))
        {
            ++localOnly;
            return;
        }
        Event wire = event;
        wire.source = toWire(wire.source);
        wire.target = toWire(wire.target);
        client.sendEvent(wire); });
}

NetEventBridge::~NetEventBridge()
{
    events.unsubscribe(subscription);
}

void NetEventBridge::setObjectMappers(ObjectMapper wireMapper, ObjectMapper localMapper)
{
    if (wireMapper)
    {
        toWire = std::move(wireMapper);
    }
    if (localMapper)
    {
        fromWire = std::move(localMapper);
    }
}

std::size_t NetEventBridge::pump()
{
    std::vector<Event> inbound = client.takeReceivedEvents();
    // Local replay owns the simulation: live network events must not touch it. They were
    // already acknowledged, so they are dropped, not held back for later.
    if (events.isPlaybackMode())
    {
        discarded += inbound.size();
        return 0;
    }
    std::size_t raised = 0;
    for (Event &event : inbound)
    {
        if (EventObjects::isOffline(event.source) || EventObjects::isOffline(event.target))
        {
            ++discarded;
            continue;
        }
        event.source = fromWire(event.source);
        event.target = fromWire(event.target);
        events.raise(std::move(event));
        ++raised;
    }
    return raised;
}
