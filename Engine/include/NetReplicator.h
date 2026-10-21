#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "GameObjectManager.h"
#include "NetProtocol.h"

class GameObject;

class NetReplicator
{
public:
    using SpawnFn = std::function<void(GameObject &object, const ObjectState &state)>;
    using DespawnFn = std::function<void(GameObject &object)>;

    static bool capture(const GameObject &object, ObjectState &out);
    static void apply(GameObject &object, const ObjectState &state);

    void setLocalOwner(uint32_t owner);
    uint32_t getLocalOwner() const;

    void publish(const GameObjectManager &objects);
    void setOutgoing(std::vector<ObjectState> states);
    std::size_t sync(GameObjectManager &objects, const SpawnFn &onSpawn = nullptr,
                     const DespawnFn &onDespawn = nullptr);

    std::vector<ObjectState> takeOutgoing();
    void replaceAll(const std::vector<ObjectState> &states);
    void replaceOwner(uint32_t owner, const std::vector<ObjectState> &states);
    void forgetOwner(uint32_t owner);
    std::vector<ObjectState> remoteStates() const;
    std::size_t remoteCount() const;

private:
    mutable std::mutex mutex;
    uint32_t localOwner = 0;
    std::vector<ObjectState> outgoing;
    std::unordered_map<uint64_t, ObjectState> remote;
    uint64_t remoteVersion = 0;

    std::unordered_map<uint64_t, uint32_t> replicaIds;
    uint64_t appliedVersion = ~0ull;
};
