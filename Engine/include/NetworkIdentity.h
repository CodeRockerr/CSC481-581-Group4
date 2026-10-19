#pragma once
#include <cstdint>
#include "Component.h"
#include "NetProtocol.h"

// Marks a GameObject as replicated over the network.
//
// - ownerId: who is authoritative for this object. 0 = server; otherwise a client id (client-server)
//   or a peer's port (peer-to-peer). Only the owner sends this object's state.
// - netId: the object's id inside its owner. 0 means "use my local GameObject id", which is what
//   locally created objects want. Replicas store the owner's netId here.
// - kind: game-defined tag (NetKind) the receiver uses to decide which components to build.
// - isReplica: true on copies that mirror someone else's object. Game code should not move these.
struct NetworkIdentity : Component
{
    NetworkIdentity() = default;
    NetworkIdentity(uint32_t ownerId, uint16_t kind, uint32_t netId = 0)
        : ownerId(ownerId), netId(netId), kind(kind) {}

    uint32_t ownerId = 0; // (Component::owner is the GameObject pointer; this is the network owner)
    uint32_t netId = 0;
    uint16_t kind = NetKind::Unknown;
    bool isReplica = false;

    // Last received velocity for replicas (useful for choosing animations).
    float lastVelocityX = 0.0f;
    float lastVelocityY = 0.0f;
};
