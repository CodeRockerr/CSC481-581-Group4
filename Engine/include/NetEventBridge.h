#pragma once
// Connects the local EventManager to a NetworkClient (Milestone 4).
//
//   local producer -> EventManager.raise -> (handled locally, immediately responsive)
//        -> bridge sees Local + shareable event -> NetworkClient::sendEvent -> server
//   server -> NetworkClient inbox -> bridge.pump() on the main thread -> EventManager.raise
//
// Received events are raised with Origin::Network and shareable=false, so they are handled
// like any other event but are never re-published (no echo loops). Handlers always run in
// EventManager::dispatch on the main thread, never on a network thread.
#include <functional>

#include "EventManager.h"
#include "NetworkClient.h"

class NetEventBridge
{
public:
    using ObjectMapper = std::function<EventObject(const EventObject &)>;

    // Construct AFTER client.connect(): the manager's owner id becomes the client id so
    // EventIds are unique across the session.
    NetEventBridge(EventManager &events, NetworkClient &client);
    ~NetEventBridge();

    NetEventBridge(const NetEventBridge &) = delete;
    NetEventBridge &operator=(const NetEventBridge &) = delete;

    // Move network-delivered events into the EventManager queue. Call once per frame from
    // the thread that calls EventManager::dispatch. Returns the number raised.
    std::size_t pump();

    // Object identity. Local objects use owner 0; on the wire they are (clientId, netId).
    // Defaults translate owner 0 <-> this client's id. Games that need more (for example
    // mapping a remote (owner, netId) to a local GameObject) override these.
    void setObjectMappers(ObjectMapper toWire, ObjectMapper fromWire);

private:
    EventManager &events;
    NetworkClient &client;
    EventManager::Subscription subscription = 0;
    ObjectMapper toWire;
    ObjectMapper fromWire;
};
