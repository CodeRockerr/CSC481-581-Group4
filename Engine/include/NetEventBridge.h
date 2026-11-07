#pragma once
// Connects the local EventManager to a NetworkClient (Milestone 4).
//
//   local producer -> EventManager.raise -> (handled locally, immediately responsive)
//        -> bridge sees Local + shareable event -> NetworkClient::sendEvent -> server
//   server -> NetworkClient inbox -> bridge.pump() on the main thread -> EventManager.raise
//
// Offline references (EventObjects::OfflineOwner) and replay state stay local: such events are
// never sent, and while EventManager is in playback mode pump() discards network events.
// Received events are raised with Origin::Network and shareable=false, so they are handled
// like any other event but are never re-published (no echo loops). Handlers always run in
// EventManager::dispatch on the main thread, never on a network thread.
#include <functional>

#include "EventManager.h"
#include "EventObjects.h"
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

    void setObjectMappers(ObjectMapper toWire, ObjectMapper fromWire);

    // Events that were raised locally but kept off the network because they reference an
    // offline object (EventObjects::OfflineOwner).
    std::size_t getLocalOnlyCount() const { return localOnly; }
    // Network events thrown away instead of raised: they arrived during local replay
    // playback, or they carried an offline reference.
    std::size_t getDiscardedCount() const { return discarded; }

private:
    EventManager &events;
    NetworkClient &client;
    EventManager::Subscription subscription = 0;
    ObjectMapper toWire;
    ObjectMapper fromWire;
    std::size_t localOnly = 0;
    std::size_t discarded = 0;
};
