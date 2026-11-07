// Milestone 4: network event serialization, routing, ordering, acknowledgment, duplicates.
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <zmq.h>

#include "Event.h"
#include "EventCodec.h"
#include "EventManager.h"
#include "NetEventBridge.h"
#include "NetProtocol.h"
#include "NetworkClient.h"
#include "Server.h"

namespace
{
    int checks = 0;

    void require(bool condition, const char *what)
    {
        ++checks;
        if (!condition)
        {
            std::cerr << "FAILED: " << what << '\n';
            std::exit(1);
        }
    }

    Event make(EventType type, EventPayload payload, EventObject source = {}, EventObject target = {})
    {
        Event e;
        e.type = type;
        e.timestamp = 123456;
        e.priority = EventPriority::High;
        e.id = {9, 77};
        e.source = source;
        e.target = target;
        e.payload = std::move(payload);
        return e;
    }

    bool same(const Event &a, const Event &b)
    {
        if (a.type != b.type || a.timestamp != b.timestamp || a.priority != b.priority || !(a.id == b.id) ||
            !(a.source == b.source) || !(a.target == b.target) || a.payload.index() != b.payload.index())
        {
            return false;
        }
        if (const auto *x = std::get_if<CollisionEvent>(&a.payload))
        {
            const auto &y = std::get<CollisionEvent>(b.payload);
            return x->normalX == y.normalX && x->normalY == y.normalY && x->trigger == y.trigger;
        }
        if (const auto *x = std::get_if<DeathEvent>(&a.payload))
            return x->reason == std::get<DeathEvent>(b.payload).reason;
        if (const auto *x = std::get_if<SpawnEvent>(&a.payload))
            return x->spawnPoint == std::get<SpawnEvent>(b.payload).spawnPoint;
        if (const auto *x = std::get_if<InputEvent>(&a.payload))
        {
            const auto &y = std::get<InputEvent>(b.payload);
            return x->action == y.action && x->value == y.value;
        }
        if (const auto *x = std::get_if<EventFields>(&a.payload))
            return *x == std::get<EventFields>(b.payload);
        return true;
    }

    std::vector<Event> samples()
    {
        EventFields fields;
        fields["lives"] = int64_t(-3);
        fields["speed"] = 0.1 + 0.2;
        fields["alive"] = true;
        fields["name"] = std::string("two words\nand a newline % ~");
        fields[""] = std::string();
        return {
            make(EventTypes::Collision, CollisionEvent{-0.5f, 1.0f / 3.0f, true}, {1, 2}, {3, 4}),
            make(EventTypes::Death, DeathEvent{"fell into lava pit"}, {0, 0}, {5, 6}),
            make(EventTypes::Death, DeathEvent{""}),
            make(EventTypes::Spawn, SpawnEvent{"checkpoint 2"}, {}, {7, 8}),
            make(EventTypes::Input, InputEvent{"jump", 0.75f}, {}, {1, 1}),
            make(EventTypes::FirstGame + 5, fields),
            make(EventTypes::FirstGame + 6, std::monostate{}),
        };
    }

    void testCodec()
    {
        for (const Event &e : samples())
        {
            std::string bin;
            EventCodec::encodeBinary(e, bin);
            Event back;
            require(EventCodec::decodeBinary(bin.data(), bin.size(), back), "binary decode");
            require(same(e, back), "binary round trip");

            for (std::size_t cut = 0; cut < bin.size(); ++cut)
            {
                Event junk;
                require(!EventCodec::decodeBinary(bin.data(), cut, junk), "truncated binary rejected");
            }
            std::string extra = bin + 'x';
            Event junk;
            require(!EventCodec::decodeBinary(extra.data(), extra.size(), junk), "trailing binary byte rejected");

            std::string text;
            EventCodec::encodeText(e, text);
            require(text.find('\n') == std::string::npos, "text event is a single line");
            Event textBack;
            require(EventCodec::decodeText(text.data(), text.data() + text.size(), textBack), "text decode");
            require(same(e, textBack), "text round trip");
        }
        Event zero = make(EventTypes::Death, DeathEvent{"x"});
        zero.type = 0;
        std::string bin;
        EventCodec::encodeBinary(zero, bin);
        Event out;
        require(!EventCodec::decodeBinary(bin.data(), bin.size(), out), "type zero rejected");
    }

    void testPacket()
    {
        // Legacy compatibility: no events and no ack -> exactly the Milestone 3 size.
        Packet plain;
        plain.type = PacketType::State;
        plain.objects.resize(3);
        for (WireFormat f : {WireFormat::Binary, WireFormat::Text})
        {
            std::string wire = NetProtocol::encode(f, plain);
            if (f == WireFormat::Binary)
            {
                require(wire.size() == sizeof(PacketHeader) + 3 * sizeof(ObjectState), "legacy size unchanged");
            }
            Packet decoded;
            require(NetProtocol::decode(wire.data(), wire.size(), decoded), "legacy decode");
            require(decoded.events.empty() && decoded.eventAck == 0, "legacy has no events");
        }

        Packet full = plain;
        full.eventAck = 41;
        uint64_t seq = 5;
        for (const Event &e : samples())
        {
            full.events.push_back({seq++, e});
        }
        for (WireFormat f : {WireFormat::Binary, WireFormat::Text})
        {
            std::string wire = NetProtocol::encode(f, full);
            Packet decoded;
            decoded.events.resize(2); // decode must reset stale state
            require(NetProtocol::decode(wire.data(), wire.size(), decoded), "event packet decode");
            require(decoded.eventAck == 41, "ack preserved");
            require(decoded.objects.size() == 3, "objects preserved");
            require(decoded.events.size() == full.events.size(), "event count preserved");
            for (std::size_t i = 0; i < full.events.size(); ++i)
            {
                require(decoded.events[i].seq == full.events[i].seq, "seq preserved");
                require(same(decoded.events[i].event, full.events[i].event), "packet event preserved");
            }
            if (f == WireFormat::Binary)
            {
                for (std::size_t cut = sizeof(PacketHeader) + 3 * sizeof(ObjectState) + 1; cut < wire.size(); ++cut)
                {
                    Packet junk;
                    require(!NetProtocol::decode(wire.data(), cut, junk), "truncated trailer rejected");
                }
            }
        }
    }

    // --------------------------------------------------------------- loopback

    struct Node
    {
        EventManager manager;
        NetworkClient client;
        std::unique_ptr<NetEventBridge> bridge;
        std::vector<Event> received;

        Node(uint16_t port, WireFormat format)
            : manager([]
                      { return EventTime(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now().time_since_epoch())
                                             .count()); }),
              client("127.0.0.1", port, format)
        {
            require(client.connect(), "client connects");
            bridge = std::make_unique<NetEventBridge>(manager, client);
            manager.subscribeAll([this](const Event &e)
                                 { received.push_back(e); });
        }

        // Run pump + dispatch for up to `ms`, or until `done` is true.
        template <typename Done>
        void spin(int ms, Done done)
        {
            const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            while (std::chrono::steady_clock::now() < end && !done())
            {
                bridge->pump();
                manager.dispatch();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        void spin(int ms)
        {
            spin(ms, [] { return false; });
        }

        std::size_t count(EventType type, EventOrigin origin) const
        {
            std::size_t n = 0;
            for (const Event &e : received)
                if (e.type == type && e.origin == origin)
                    ++n;
            return n;
        }
    };

    void testLoopback(WireFormat format, uint16_t port)
    {
        Server server(port);
        server.setEventFilter([](const Event &e, uint32_t)
                              { return e.type != EventTypes::Input; });
        std::thread serverThread([&]
                                 { server.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        {
            Node a(port, format);
            Node b(port, format);
            require(a.client.getClientId() != b.client.getClientId(), "distinct client ids");
            const uint32_t idA = a.client.getClientId();

            // 1. One shareable event crosses the network and arrives intact.
            Event death;
            death.type = EventTypes::Death;
            death.shareable = true;
            death.payload = DeathEvent{"fell into lava"};
            death.target = {0, 7}; // local object 7 -> (idA, 7) on the wire
            a.manager.raise(death);
            a.spin(50);
            b.spin(3000, [&]
                   { return b.count(EventTypes::Death, EventOrigin::Network) >= 1; });
            require(b.count(EventTypes::Death, EventOrigin::Network) == 1, "B received the Death event once");
            const Event *got = nullptr;
            for (const Event &e : b.received)
                if (e.type == EventTypes::Death)
                    got = &e;
            require(got != nullptr && std::get<DeathEvent>(got->payload).reason == "fell into lava", "payload intact");
            require(got->id.ownerId == idA, "server stamped the sender id");
            require(got->target == EventObject{idA, 7}, "target identity mapped to (owner, netId)");
            require(!got->shareable, "received events are not re-shareable");
            require(a.count(EventTypes::Death, EventOrigin::Network) == 0, "sender is not echoed its own event");
            require(a.count(EventTypes::Death, EventOrigin::Local) == 1, "sender handled it locally");

            // 2. Ordering and exactly-once for a burst.
            for (int i = 0; i < 150; ++i)
            {
                Event spawn;
                spawn.type = EventTypes::Spawn;
                spawn.shareable = true;
                spawn.payload = SpawnEvent{std::to_string(i)};
                a.manager.raise(spawn);
            }
            a.spin(50);
            b.spin(5000, [&]
                   { return b.count(EventTypes::Spawn, EventOrigin::Network) >= 150; });
            std::vector<int> order;
            for (const Event &e : b.received)
                if (e.type == EventTypes::Spawn)
                    order.push_back(std::stoi(std::get<SpawnEvent>(e.payload).spawnPoint));
            require(order.size() == 150, "burst delivered exactly once (more than one packet)");
            for (int i = 0; i < 150; ++i)
                require(order[i] == i, "burst delivered in order");
            b.spin(300);
            require(b.count(EventTypes::Spawn, EventOrigin::Network) == 150, "no late duplicates");

            // 3. Non-shareable and replay-control events stay local.
            Event local;
            local.type = EventTypes::Collision;
            local.shareable = false;
            local.payload = CollisionEvent{};
            a.manager.raise(local);
            Event control;
            control.type = EventTypes::ReplayStart;
            control.shareable = true;
            a.manager.raise(control);
            a.spin(50);
            b.spin(500);
            require(b.count(EventTypes::Collision, EventOrigin::Network) == 0, "non-shareable not sent");
            require(b.count(EventTypes::ReplayStart, EventOrigin::Network) == 0, "replay control not sent");

            // 4. The authoritative filter can reject; rejection is acknowledged, not resent.
            Event input;
            input.type = EventTypes::Input;
            input.shareable = true;
            input.payload = InputEvent{"jump", 1};
            a.manager.raise(input);
            a.spin(50);
            b.spin(500);
            require(b.count(EventTypes::Input, EventOrigin::Network) == 0, "filtered event not forwarded");
            require(server.getEventsRejected() == 1, "server counted the rejection");
            require(a.client.pendingOutgoingEvents() == 0, "everything acknowledged, nothing left to retransmit");

            // 5. Both directions work, and server-authored events reach everyone.
            Event reply;
            reply.type = EventTypes::Death;
            reply.shareable = true;
            reply.payload = DeathEvent{"b died"};
            b.manager.raise(reply);
            b.spin(50);
            a.spin(3000, [&]
                   { return a.count(EventTypes::Death, EventOrigin::Network) >= 1; });
            require(a.count(EventTypes::Death, EventOrigin::Network) == 1, "B -> A delivery");

            Event announce;
            announce.type = EventTypes::FirstGame + 1;
            announce.payload = EventFields{{"round", int64_t(2)}};
            server.broadcastEvent(announce);
            a.spin(3000, [&]
                   { return a.count(EventTypes::FirstGame + 1, EventOrigin::Network) >= 1; });
            b.spin(3000, [&]
                   { return b.count(EventTypes::FirstGame + 1, EventOrigin::Network) >= 1; });
            require(a.count(EventTypes::FirstGame + 1, EventOrigin::Network) == 1, "server broadcast reaches A");
            require(b.count(EventTypes::FirstGame + 1, EventOrigin::Network) == 1, "server broadcast reaches B");
            require(server.getDuplicateEventsDropped() == 0, "no duplicates in the clean run");
        }

        server.stop();
        serverThread.join();
    }

    // ------------------------------------------------------ raw duplicate / gap

    bool roundTrip(void *socket, const Packet &request, Packet &reply)
    {
        std::string wire = NetProtocol::encode(WireFormat::Binary, request);
        if (zmq_send(socket, wire.data(), wire.size(), 0) < 0)
            return false;
        char buffer[65536];
        const int n = zmq_recv(socket, buffer, sizeof(buffer), 0);
        return n > 0 && NetProtocol::decode(buffer, static_cast<std::size_t>(n), reply);
    }

    void *openLink(void *context, uint16_t port, uint32_t &clientId)
    {
        void *join = zmq_socket(context, ZMQ_REQ);
        const int timeout = 2000, linger = 0;
        zmq_setsockopt(join, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        zmq_setsockopt(join, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_connect(join, ("tcp://127.0.0.1:" + std::to_string(port)).c_str());
        Packet request, reply;
        request.type = PacketType::Join;
        require(roundTrip(join, request, reply) && reply.type == PacketType::JoinAccept, "raw join");
        zmq_close(join);
        clientId = reply.sender;
        void *link = zmq_socket(context, ZMQ_REQ);
        zmq_setsockopt(link, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        zmq_setsockopt(link, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_connect(link, ("tcp://127.0.0.1:" + std::to_string(reply.aux)).c_str());
        return link;
    }

    NetEvent netEvent(uint64_t seq, const char *tag)
    {
        Event e;
        e.type = EventTypes::Spawn;
        e.payload = SpawnEvent{tag};
        return {seq, e};
    }

    void testDuplicatesAndGaps(uint16_t port)
    {
        Server server(port);
        std::thread serverThread([&]
                                 { server.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        void *context = zmq_ctx_new();

        uint32_t idA = 0, idB = 0;
        void *a = openLink(context, port, idA);
        void *b = openLink(context, port, idB);

        Packet state, reply;
        state.type = PacketType::State;
        state.sender = idA;

        // seq 1, then the same seq 1 again (lost-reply retransmit), then seq 3 (gap), then 2.
        state.events = {netEvent(1, "one")};
        require(roundTrip(a, state, reply), "A sends 1");
        require(reply.eventAck == 1, "server acks seq 1");
        state.events = {netEvent(1, "one"), netEvent(2, "two")};
        require(roundTrip(a, state, reply), "A resends 1 with 2");
        require(reply.eventAck == 2, "server acks through 2, no double count");
        require(server.getDuplicateEventsDropped() == 1, "duplicate of seq 1 dropped");
        state.events = {netEvent(4, "four")};
        require(roundTrip(a, state, reply), "A skips ahead");
        require(reply.eventAck == 2, "gap is not acknowledged");
        state.events = {netEvent(3, "three"), netEvent(4, "four")};
        require(roundTrip(a, state, reply), "A fills the gap");
        require(reply.eventAck == 4, "ack advances after gap is filled");
        require(server.getEventsRouted() == 4, "each of the four events routed exactly once");

        // B drains its queue, never acks, and sees the same events again (no loss);
        // after acking, they are gone.
        Packet poll;
        poll.type = PacketType::State;
        poll.sender = idB;
        require(roundTrip(b, poll, reply), "B polls");
        require(reply.events.size() == 4, "B has 4 queued events");
        for (std::size_t i = 0; i < 4; ++i)
            require(reply.events[i].seq == i + 1, "B's link seq is contiguous");
        require(std::get<SpawnEvent>(reply.events[0].event.payload).spawnPoint == "one", "order: one first");
        require(std::get<SpawnEvent>(reply.events[3].event.payload).spawnPoint == "four", "order: four last");
        require(reply.events[0].event.id.ownerId == idA, "server rewrote id owner");
        require(roundTrip(b, poll, reply) && reply.events.size() == 4, "unacked events are redelivered");
        poll.eventAck = 4;
        require(roundTrip(b, poll, reply) && reply.events.empty(), "acked events are released");

        // Spoofed owner id is overwritten by the server.
        NetEvent spoof = netEvent(1, "spoof");
        spoof.event.id = {idB, 5};
        Packet fromB;
        fromB.type = PacketType::State;
        fromB.sender = idB;
        fromB.eventAck = 4;
        fromB.events = {spoof};
        require(roundTrip(b, fromB, reply), "B sends");
        Packet pollA;
        pollA.type = PacketType::State;
        pollA.sender = idA;
        pollA.eventAck = 0;
        require(roundTrip(a, pollA, reply) && !reply.events.empty(), "A receives B's event");
        require(reply.events.back().event.id.ownerId == idB, "id owner is the authenticated sender");

        zmq_close(a);
        zmq_close(b);
        zmq_ctx_destroy(context);
        server.stop();
        serverThread.join();
    }

    // ------------------------------------------------- offline refs and local replay

    void testOfflineRefsAndPlayback(uint16_t port)
    {
        Server server(port);
        std::thread serverThread([&]
                                 { server.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        {
            Node a(port, WireFormat::Binary);
            Node b(port, WireFormat::Binary);

            // 1. An event that names an offline object stays on the machine that raised it.
            Event offline;
            offline.type = EventTypes::Death;
            offline.shareable = true;
            offline.payload = DeathEvent{"offline"};
            offline.target = {EventObjects::OfflineOwner, 3};
            a.manager.raise(offline);
            a.spin(50);
            b.spin(400);
            require(b.count(EventTypes::Death, EventOrigin::Network) == 0, "offline-reference event never reaches B");
            require(a.count(EventTypes::Death, EventOrigin::Local) == 1, "A still handled its own offline event");
            require(a.bridge->getLocalOnlyCount() == 1, "bridge counted the event it kept local");

            // 2. A normal event right after it still goes through (the filter did not wedge the link).
            Event normal;
            normal.type = EventTypes::Death;
            normal.shareable = true;
            normal.payload = DeathEvent{"normal"};
            normal.target = {0, 4};
            a.manager.raise(normal);
            a.spin(50);
            b.spin(3000, [&]
                   { return b.count(EventTypes::Death, EventOrigin::Network) >= 1; });
            require(b.count(EventTypes::Death, EventOrigin::Network) == 1, "normal event delivered once");

            // 3. Local replay: network events are discarded while playing back...
            b.manager.setPlaybackMode(true);
            Event during;
            during.type = EventTypes::Death;
            during.shareable = true;
            during.payload = DeathEvent{"during playback"};
            during.target = {0, 5};
            a.manager.raise(during);
            a.spin(50);
            b.spin(500);
            require(b.count(EventTypes::Death, EventOrigin::Network) == 1, "no live event reaches B during playback");
            require(b.bridge->getDiscardedCount() >= 1, "bridge discarded the event that arrived during playback");

            // ...and are delivered again once playback ends (and the discarded one is not replayed late).
            b.manager.setPlaybackMode(false);
            Event after;
            after.type = EventTypes::Death;
            after.shareable = true;
            after.payload = DeathEvent{"after playback"};
            after.target = {0, 6};
            a.manager.raise(after);
            a.spin(50);
            b.spin(3000, [&]
                   { return b.count(EventTypes::Death, EventOrigin::Network) >= 2; });
            require(b.count(EventTypes::Death, EventOrigin::Network) == 2, "exactly one more event after playback ends");
        }

        // 4. The server itself refuses offline references from a client that does not use the bridge.
        void *context = zmq_ctx_new();
        uint32_t idA = 0, idB = 0;
        void *rawA = openLink(context, port, idA);
        void *rawB = openLink(context, port, idB);
        const uint64_t rejectedBefore = server.getEventsRejected();
        NetEvent bad = netEvent(1, "bad");
        bad.event.source = {EventObjects::OfflineOwner, 9};
        Packet state, reply;
        state.type = PacketType::State;
        state.sender = idA;
        state.events = {bad};
        require(roundTrip(rawA, state, reply), "raw client sends an offline reference");
        require(reply.eventAck == 1, "event is acknowledged so the client stops resending it");
        require(server.getEventsRejected() == rejectedBefore + 1, "server counted the rejection");
        Packet poll;
        poll.type = PacketType::State;
        poll.sender = idB;
        require(roundTrip(rawB, poll, reply) && reply.events.empty(), "nothing was forwarded to the other client");
        zmq_close(rawA);
        zmq_close(rawB);
        zmq_ctx_destroy(context);

        server.stop();
        serverThread.join();
    }

    // ------------------------------------------------------------ disconnected clients

    bool waitFor(int ms, const std::function<bool()> &condition)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end)
        {
            if (condition())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return condition();
    }

    void testDisconnects(uint16_t port)
    {
        Server server(port);
        server.setClientTimeoutMs(600);
        std::thread serverThread([&]
                                 { server.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        void *context = zmq_ctx_new();

        uint32_t idA = 0, idB = 0, idC = 0;
        void *a = openLink(context, port, idA);
        void *b = openLink(context, port, idB);
        void *c = openLink(context, port, idC);
        require(waitFor(2000, [&]
                        { return server.getClientCount() == 3 && server.getEventLinkCount() == 3; }),
                "three clients, three delivery queues");

        // A sends an event, then leaves cleanly. The event was already queued for the others.
        Packet state, reply;
        state.type = PacketType::State;
        state.sender = idA;
        state.events = {netEvent(1, "from A")};
        require(roundTrip(a, state, reply) && reply.eventAck == 1, "A's event is accepted");
        Packet leave;
        leave.type = PacketType::Leave;
        leave.sender = idA;
        require(roundTrip(a, leave, reply), "A leaves");
        require(waitFor(2000, [&]
                        { return server.getClientCount() == 2 && server.getEventLinkCount() == 2; }),
                "A's delivery queue is released when it leaves");

        Packet pollB;
        pollB.type = PacketType::State;
        pollB.sender = idB;
        require(roundTrip(b, pollB, reply) && reply.events.size() == 1, "B still gets the event A sent before leaving");
        require(std::get<SpawnEvent>(reply.events[0].event.payload).spawnPoint == "from A", "and it is the right one");
        pollB.eventAck = reply.events[0].seq;

        // A server broadcast after A left is queued only for the clients that are still here.
        Event notice;
        notice.type = EventTypes::Spawn;
        notice.payload = SpawnEvent{"server notice"};
        server.broadcastEvent(notice);
        require(roundTrip(b, pollB, reply) && reply.events.size() == 1, "B receives the broadcast");
        pollB.eventAck = reply.events[0].seq;

        // C goes silent without leaving (a crash). The server drops it after the timeout.
        // B keeps polling meanwhile, because a client that stops polling would time out too.
        require(waitFor(5000, [&]
                        {
                            Packet keepAlive;
                            roundTrip(b, pollB, keepAlive);
                            return server.getClientCount() == 1 && server.getEventLinkCount() == 1;
                        }),
                "a silent client times out and its queue is released");
        require(roundTrip(b, pollB, reply) && reply.events.empty(), "B is unaffected by the others disconnecting");

        // A new client gets a fresh link: nothing from the departed clients, sequence from 1.
        uint32_t idD = 0;
        void *d = openLink(context, port, idD);
        require(idD != idA && idD != idC, "a new client gets a new id");
        server.broadcastEvent(notice);
        Packet pollD;
        pollD.type = PacketType::State;
        pollD.sender = idD;
        require(roundTrip(d, pollD, reply) && reply.events.size() == 1, "new client sees only events after it joined");
        require(reply.events[0].seq == 1, "its delivery sequence starts at 1");

        zmq_close(a);
        zmq_close(b);
        zmq_close(c);
        zmq_close(d);
        zmq_ctx_destroy(context);
        server.stop();
        serverThread.join();
    }
}

int main()
{
    testCodec();
    testPacket();
    testLoopback(WireFormat::Binary, 5611);
    testLoopback(WireFormat::Text, 5612);
    testDuplicatesAndGaps(5613);
    testOfflineRefsAndPlayback(5614);
    testDisconnects(5615);
    std::cout << "net_events_test passed (" << checks << " checks)\n";
    return 0;
}
