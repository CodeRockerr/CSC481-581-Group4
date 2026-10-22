#include "Components.h"
#include "GameObjectManager.h"
#include "NetProtocol.h"
#include "NetworkClient.h"
#include "NetworkIdentity.h"
#include "PeerSession.h"
#include "Server.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool condition, const std::string &what)
    {
        std::cout << (condition ? "  PASS  " : "  FAIL  ") << what << '\n';
        if (!condition)
        {
            ++failures;
        }
    }

    using Clock = std::chrono::steady_clock;

    struct TestClient
    {
        std::unique_ptr<NetworkClient> net;
        GameObjectManager objects;
        uint32_t playerId = 0;
        std::size_t replicas = 0;

        void tick(float x)
        {
            if (GameObject *player = objects.find(playerId))
            {
                player->get<Transform>()->x = x;
            }
            net->publish(objects);
            replicas = net->sync(objects);
            objects.flushDestroyed();
        }

        std::size_t countKind(uint16_t kind) const
        {
            std::size_t n = 0;
            for (const auto &object : objects.getAll())
            {
                const NetworkIdentity *identity = object->get<NetworkIdentity>();
                if (identity != nullptr && identity->isReplica && identity->kind == kind)
                {
                    ++n;
                }
            }
            return n;
        }

        const GameObject *replicaOf(uint32_t owner) const
        {
            for (const auto &object : objects.getAll())
            {
                const NetworkIdentity *identity = object->get<NetworkIdentity>();
                if (identity != nullptr && identity->isReplica && identity->ownerId == owner)
                {
                    return object.get();
                }
            }
            return nullptr;
        }
    };

    bool runFrames(std::vector<TestClient *> clients, int timeoutMs, const std::function<bool()> &done)
    {
        const Clock::time_point end = Clock::now() + std::chrono::milliseconds(timeoutMs);
        int frame = 0;
        while (Clock::now() < end)
        {
            for (TestClient *client : clients)
            {
                client->tick(100.0f + static_cast<float>(client->net->getClientId()) * 10.0f +
                             static_cast<float>(frame % 50));
            }
            if (done())
            {
                return true;
            }
            ++frame;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        return false;
    }

    void testCodecs()
    {
        std::cout << "Codec round trip\n";
        Packet packet;
        packet.type = PacketType::Snapshot;
        packet.sender = 42;
        packet.tic = 123456789;
        packet.aux = 7;
        for (int i = 0; i < 3; ++i)
        {
            ObjectState s;
            s.netId = 10 + i;
            s.owner = i;
            s.kind = static_cast<uint16_t>(100 + i);
            s.flags = NetFlags::Visible | (i % 2 ? NetFlags::FlipHorizontal : 0);
            s.frame = static_cast<uint8_t>(i);
            s.x = 1.25f * i;
            s.y = -3.5f;
            s.width = 32.0f;
            s.height = 48.0f;
            s.velocityX = 200.125f;
            s.velocityY = -700.0f;
            packet.objects.push_back(s);
        }
        for (WireFormat format : {WireFormat::Binary, WireFormat::Text})
        {
            const std::string bytes = NetProtocol::encode(format, packet);
            Packet out;
            const bool ok = NetProtocol::decode(bytes.data(), bytes.size(), out);
            bool same = ok && out.type == packet.type && out.sender == 42 && out.tic == 123456789 && out.aux == 7 &&
                        out.objects.size() == 3;
            for (std::size_t i = 0; same && i < 3; ++i)
            {
                const ObjectState &a = packet.objects[i];
                const ObjectState &b = out.objects[i];
                same = a.netId == b.netId && a.owner == b.owner && a.kind == b.kind && a.flags == b.flags &&
                       a.frame == b.frame && std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.velocityX - b.velocityX) < 1e-3f;
            }
            check(same, std::string(NetProtocol::formatName(format)) + " round trip (" + std::to_string(bytes.size()) +
                            " bytes for 3 objects)");
        }
        const char garbage[] = "Xnot a packet";
        Packet out;
        check(!NetProtocol::decode(garbage, sizeof(garbage), out), "garbage rejected");
        const std::string truncated = NetProtocol::encode(WireFormat::Binary, packet).substr(0, 40);
        check(!NetProtocol::decode(truncated.data(), truncated.size(), out), "truncated binary rejected");
    }
}

int main()
{
    testCodecs();

    const uint16_t port = 5710;
    Server server(port);
    server.setClientTimeoutMs(800);

    GameObject &horizontal = server.createObject(NetKind::Platform, "h-platform");
    horizontal.add<Transform>(500.0f, 400.0f, 200.0f, 20.0f);
    GameObject &vertical = server.createObject(NetKind::Platform, "v-platform");
    vertical.add<Transform>(900.0f, 300.0f, 200.0f, 20.0f);
    const uint32_t hId = horizontal.getId();
    const uint32_t vId = vertical.getId();
    server.setSceneUpdate([hId, vId](GameObjectManager &world, float, double t)
                          {
        world.find(hId)->get<Transform>()->x = 500.0f + 150.0f * static_cast<float>(std::sin(t));
        world.find(vId)->get<Transform>()->y = 300.0f + 100.0f * static_cast<float>(std::sin(t)); });

    std::thread serverThread([&]
                             { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "Four clients\n";
    std::vector<std::unique_ptr<TestClient>> owned;
    std::vector<TestClient *> clients;
    for (int i = 0; i < 4; ++i)
    {
        auto client = std::make_unique<TestClient>();
        const WireFormat format = (i % 2 == 0) ? WireFormat::Binary : WireFormat::Text;
        client->net = std::make_unique<NetworkClient>("127.0.0.1", port, format);
        check(client->net->connect(), "client " + std::to_string(i + 1) + " connects (" +
                                           NetProtocol::formatName(format) + ")");
        GameObject &player = client->objects.create("player");
        player.add<Transform>(100.0f, 100.0f, 40.0f, 40.0f);
        player.add<Sprite>();
        player.add<NetworkIdentity>(client->net->getClientId(), NetKind::Player);
        client->playerId = player.getId();
        clients.push_back(client.get());
        owned.push_back(std::move(client));
    }

    const bool converged = runFrames(clients, 3000, [&]
                                     {
        for (TestClient *c : clients)
        {
            if (c->countKind(NetKind::Player) != 3 || c->countKind(NetKind::Platform) != 2)
                return false;
        }
        return true; });
    check(converged, "every client sees 3 remote players + 2 platforms");
    check(server.getClientCount() == 4, "server reports 4 clients");

    {
        auto platformX = [](const TestClient &c)
        {
            for (const auto &object : c.objects.getAll())
            {
                const NetworkIdentity *id = object->get<NetworkIdentity>();
                if (id && id->isReplica && id->kind == NetKind::Platform && object->get<Transform>()->width == 200.0f &&
                    object->get<Transform>()->y == 400.0f)
                    return object->get<Transform>()->x;
            }
            return -1.0f;
        };
        const float before = platformX(*clients[0]);
        runFrames(clients, 300, []
                  { return false; });
        const float after = platformX(*clients[0]);
        check(before > 0.0f && after > 0.0f && before != after, "moving platform updates on clients");
        const float c3 = platformX(*clients[2]);
        check(std::fabs(c3 - after) < 30.0f, "platform position agrees across clients (diff " +
                                                  std::to_string(std::fabs(c3 - after)) + ")");
    }

    std::cout << "Latency\n";
    {
        TestClient &mover = *clients[0];
        TestClient &watcher = *clients[1];
        const uint32_t moverOwner = mover.net->getClientId();
        double worstMs = 0.0;
        double totalMs = 0.0;
        const int trials = 20;
        for (int trial = 0; trial < trials; ++trial)
        {
            const float target = 5000.0f + static_cast<float>(trial);
            const Clock::time_point start = Clock::now();
            bool seen = false;
            while (!seen && Clock::now() - start < std::chrono::milliseconds(1000))
            {
                mover.objects.find(mover.playerId)->get<Transform>()->x = target;
                mover.net->publish(mover.objects);
                watcher.replicas = watcher.net->sync(watcher.objects);
                const GameObject *replica = watcher.replicaOf(moverOwner);
                seen = replica != nullptr && replica->get<Transform>()->x == target;
                if (!seen)
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            worstMs = std::max(worstMs, ms);
            totalMs += ms;
        }
        std::cout << "  mean " << totalMs / trials << " ms, worst " << worstMs << " ms (60 Hz send pacing)\n";
        check(worstMs < 100.0, "movement reaches another client in < 100 ms");
    }

    std::cout << "Graceful leave\n";
    {
        const uint32_t leaving = clients[3]->net->getClientId();
        clients[3]->net->disconnect();
        clients.pop_back();
        const bool removed = runFrames(clients, 2000, [&]
                                       {
            for (TestClient *c : clients)
                if (c->replicaOf(leaving) != nullptr || c->countKind(NetKind::Player) != 2)
                    return false;
            return server.getClientCount() == 3; });
        check(removed, "leaving player destroyed on all remaining clients; server reports 3");
    }

    std::cout << "Silent drop (crash without Leave)\n";
    {
        void *ctx = zmq_ctx_new();
        void *join = zmq_socket(ctx, ZMQ_REQ);
        const int linger = 0;
        zmq_setsockopt(join, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_connect(join, ("tcp://127.0.0.1:" + std::to_string(port)).c_str());
        Packet hello;
        hello.type = PacketType::Join;
        std::string bytes = NetProtocol::encode(WireFormat::Binary, hello);
        zmq_send(join, bytes.data(), bytes.size(), 0);
        char reply[256];
        const int n = zmq_recv(join, reply, sizeof(reply), 0);
        Packet accept;
        const bool joined = n > 0 && NetProtocol::decode(reply, static_cast<std::size_t>(n), accept) &&
                            accept.type == PacketType::JoinAccept;
        check(joined, "ghost client joins");
        void *game = zmq_socket(ctx, ZMQ_REQ);
        zmq_setsockopt(game, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_connect(game, ("tcp://127.0.0.1:" + std::to_string(accept.aux)).c_str());
        Packet state;
        state.type = PacketType::State;
        ObjectState ghost;
        ghost.netId = 1;
        ghost.kind = NetKind::Player;
        ghost.x = 42.0f;
        state.objects.push_back(ghost);
        bytes = NetProtocol::encode(WireFormat::Binary, state);
        zmq_send(game, bytes.data(), bytes.size(), 0);
        char big[65536];
        zmq_recv(game, big, sizeof(big), 0);

        const uint32_t ghostId = accept.sender;
        const bool appeared = runFrames(clients, 1000, [&]
                                        { return clients[0]->replicaOf(ghostId) != nullptr; });
        check(appeared, "ghost player appears on clients");
        const bool vanished = runFrames(clients, 3000, [&]
                                        {
            for (TestClient *c : clients)
                if (c->replicaOf(ghostId) != nullptr)
                    return false;
            return server.getClientCount() == 3; });
        check(vanished, "silent client timed out and removed everywhere; others unaffected");
        check(clients[0]->countKind(NetKind::Player) == 2 && clients[0]->countKind(NetKind::Platform) == 2,
              "remaining scene intact");
        zmq_close(game);
        zmq_close(join);
        zmq_ctx_destroy(ctx);
    }

    for (TestClient *c : clients)
    {
        c->net->disconnect();
    }
    server.stop();
    serverThread.join();

    std::cout << "Peer-to-peer\n";
    {
        GameObjectManager worldA, worldB;
        auto peerA = std::make_unique<PeerSession>(5801, WireFormat::Binary);
        auto peerB = std::make_unique<PeerSession>(5802, WireFormat::Text);
        peerA->connectTo("127.0.0.1", 5802);
        peerB->connectTo("127.0.0.1", 5801);
        GameObject &a = worldA.create("a");
        a.add<Transform>(10.0f, 20.0f, 30.0f, 30.0f);
        a.add<NetworkIdentity>(peerA->getOwnerId(), NetKind::Player);
        GameObject &b = worldB.create("b");
        b.add<Transform>(70.0f, 80.0f, 30.0f, 30.0f);
        b.add<NetworkIdentity>(peerB->getOwnerId(), NetKind::Player);

        bool linked = false;
        const Clock::time_point end = Clock::now() + std::chrono::seconds(3);
        while (!linked && Clock::now() < end)
        {
            peerA->publish(worldA);
            peerB->publish(worldB);
            const std::size_t seenByA = peerA->sync(worldA);
            const std::size_t seenByB = peerB->sync(worldB);
            linked = seenByA == 1 && seenByB == 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        check(linked, "two peers (binary + text) replicate each other's player");

        peerB.reset();
        bool forgotten = false;
        const Clock::time_point end2 = Clock::now() + std::chrono::seconds(3);
        while (!forgotten && Clock::now() < end2)
        {
            peerA->publish(worldA);
            forgotten = peerA->sync(worldA) == 0;
            worldA.flushDestroyed();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        check(forgotten && worldA.size() == 1, "departed peer's replica destroyed");
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES: " + std::to_string(failures) + "\n");
    return failures == 0 ? 0 : 1;
}
