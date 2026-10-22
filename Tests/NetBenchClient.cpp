#include "Components.h"
#include "GameObjectManager.h"
#include "NetworkClient.h"
#include "NetworkIdentity.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char **argv)
{
    std::string host = "127.0.0.1";
    uint16_t port = 5555;
    WireFormat format = WireFormat::Binary;
    long iterations = 10000;
    long warmup = 200;
    int owned = 1;
    int holdMs = 0;

    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string flag = argv[i];
        const std::string value = argv[i + 1];
        if (flag == "--host")
            host = value;
        else if (flag == "--port")
            port = static_cast<uint16_t>(std::stoi(value));
        else if (flag == "--format")
        {
            if (!NetProtocol::parseFormat(value, format))
            {
                std::cerr << "Unknown format " << value << '\n';
                return 1;
            }
        }
        else if (flag == "--iterations")
            iterations = std::stol(value);
        else if (flag == "--warmup")
            warmup = std::stol(value);
        else if (flag == "--owned")
            owned = std::stoi(value);
        else if (flag == "--hold-ms")
            holdMs = std::stoi(value);
        else
        {
            std::cerr << "Unknown option " << flag << '\n';
            return 1;
        }
    }

    NetworkClient client(host.c_str(), port, format);
    if (!client.connect())
    {
        return 1;
    }
    client.setSendIntervalMs(0);

    GameObjectManager objects;
    for (int i = 0; i < owned; ++i)
    {
        GameObject &object = objects.create("bench-player");
        object.add<Transform>(100.0f + 20.0f * static_cast<float>(i), 100.0f, 32.0f, 32.0f);
        object.add<PhysicsBody>(1, false);
        object.add<NetworkIdentity>(client.getClientId(), NetKind::Player);
    }

    using Clock = std::chrono::steady_clock;
    uint64_t sequence = client.getSnapshotSequence();
    std::size_t replicas = 0;
    uint64_t sentBefore = 0;
    uint64_t receivedBefore = 0;
    uint64_t messagesBefore = 0;
    double rttSum = 0.0;
    Clock::time_point start = Clock::now();

    for (long i = 0; i < warmup + iterations; ++i)
    {
        if (i == warmup)
        {
            sentBefore = client.getBytesSent();
            receivedBefore = client.getBytesReceived();
            messagesBefore = client.getMessagesSent();
            rttSum = 0.0;
            start = Clock::now();
        }

        objects.forEach<Transform>([&](GameObject &, Transform &t)
                                   { t.x = 100.0f + 50.0f * std::sin(static_cast<float>(i) * 0.01f); });
        client.publish(objects);

        const uint64_t next = client.waitForSnapshot(sequence, 2000);
        if (next == sequence)
        {
            std::cerr << "Timed out waiting for snapshot at iteration " << i << '\n';
            return 2;
        }
        sequence = next;
        replicas = client.sync(objects);
        objects.flushDestroyed();
        rttSum += static_cast<double>(client.getLastRoundTripMicros());
    }

    const double elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    const uint64_t sent = client.getBytesSent() - sentBefore;
    const uint64_t received = client.getBytesReceived() - receivedBefore;
    const uint64_t messages = client.getMessagesSent() - messagesBefore;

    std::printf("%s,%ld,%.3f,%.3f,%zu,%llu,%llu,%.1f,%.1f\n", NetProtocol::formatName(format), iterations, elapsedMs,
                iterations > 0 ? elapsedMs * 1000.0 / static_cast<double>(iterations) : 0.0, replicas,
                static_cast<unsigned long long>(sent), static_cast<unsigned long long>(received),
                messages > 0 ? static_cast<double>(received) / static_cast<double>(messages) : 0.0,
                iterations > 0 ? rttSum / static_cast<double>(iterations) : 0.0);
    std::fflush(stdout);

    if (holdMs > 0)
    {
        client.setSendIntervalMs(16);
        std::this_thread::sleep_for(std::chrono::milliseconds(holdMs));
    }
    client.disconnect();
    return 0;
}
