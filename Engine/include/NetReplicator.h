#pragma once
// Shared replication logic used by Server, NetworkClient and PeerSession.
//
// Threading contract:
//   - publish(), sync() and anything that takes a GameObjectManager run on the thread that owns
//     that manager (the game's main thread, or the server's simulation thread).
//   - takeOutgoing(), replaceAll(), replaceOwner(), forgetOwner() and remoteStates() are called
//     from network threads. All shared state is behind one mutex.
//
// The game never touches sockets. It marks objects with NetworkIdentity, calls publish() to hand
// its owned objects to the network thread, and calls sync() to turn the latest received states
// into replica GameObjects (create / update / destroy).

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
    // Called once when a new replica is created, after the engine has added Transform,
    // Sprite and NetworkIdentity. Use it to attach textures, colliders, etc. by state.kind.
    using SpawnFn = std::function<void(GameObject &object, const ObjectState &state)>;
    // Called just before a replica is destroyed (owner left or stopped sending it).
    using DespawnFn = std::function<void(GameObject &object)>;

    // Fills `out` from an object's Transform / PhysicsBody / Sprite / NetworkIdentity.
    // Returns false if the object has no Transform or no NetworkIdentity.
    static bool capture(const GameObject &object, ObjectState &out);
    // Writes a received state into a replica's components.
    static void apply(GameObject &object, const ObjectState &state);

    void setLocalOwner(uint32_t owner);
    uint32_t getLocalOwner() const;

    // ---- game thread ----
    // Captures every active object whose NetworkIdentity.owner == local owner and queues them.
    void publish(const GameObjectManager &objects);
    // Queues explicit states (used by the legacy NetMessage shim and by tests).
    void setOutgoing(std::vector<ObjectState> states);
    // Applies the most recent remote states. Returns the number of live replicas.
    std::size_t sync(GameObjectManager &objects, const SpawnFn &onSpawn = nullptr,
                     const DespawnFn &onDespawn = nullptr);

    // ---- network threads ----
    std::vector<ObjectState> takeOutgoing();
    // Replace the whole remote world (client-server: one snapshot holds everything).
    // States owned by the local owner are ignored.
    void replaceAll(const std::vector<ObjectState> &states);
    // Replace only one owner's states (server receiving a client, or one peer in P2P).
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

    // Game-thread only.
    std::unordered_map<uint64_t, uint32_t> replicaIds; // netKey -> local GameObject id
    uint64_t appliedVersion = ~0ull;
};
