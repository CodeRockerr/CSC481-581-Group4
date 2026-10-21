#pragma once
#include <cstdint>
#include "Component.h"
#include "NetProtocol.h"

struct NetworkIdentity : Component
{
    NetworkIdentity() = default;
    NetworkIdentity(uint32_t ownerId, uint16_t kind, uint32_t netId = 0)
        : ownerId(ownerId), netId(netId), kind(kind) {}

    uint32_t ownerId = 0;
    uint32_t netId = 0;
    uint16_t kind = NetKind::Unknown;
    bool isReplica = false;

    float lastVelocityX = 0.0f;
    float lastVelocityY = 0.0f;
};
