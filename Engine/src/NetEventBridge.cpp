#include "NetEventBridge.h"

NetEventBridge::NetEventBridge(EventManager &eventManager, NetworkClient &networkClient)
    : events(eventManager), client(networkClient)
{
    const uint32_t self = client.getClientId();
    events.setOwnerId(self);

    toWire = [self](const EventObject &object)
    {
        return object.ownerId == 0 ? EventObject{self, object.netId} : object;
    };
    fromWire = [self](const EventObject &object)
    {
        return object.ownerId == self ? EventObject{0, object.netId} : object;
    };

    subscription = events.subscribeAll([this](const Event &event)
                                       {
        if (!event.shareable || event.origin != EventOrigin::Local || isReplayControl(event.type))
        {
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
    std::size_t raised = 0;
    for (Event &event : client.takeReceivedEvents())
    {
        event.source = fromWire(event.source);
        event.target = fromWire(event.target);
        events.raise(std::move(event));
        ++raised;
    }
    return raised;
}
