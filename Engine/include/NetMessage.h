#pragma once
#include <cstdint>

#pragma pack(push, 1)

enum class NetMessageType : uint8_t
{
    Join = 1,
    Leave = 2,
    PlayerState = 3,
    PlatformState = 4,
};

struct NetMessage
{
    NetMessageType type = NetMessageType::Join;
    uint32_t clientId = 0;
    int64_t tic = 0;
    float x = 0.0f;
    float y = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
};

#pragma pack(pop)

static_assert(sizeof(NetMessageType) == 1, "NetMessageType must be 1 byte");
static_assert(sizeof(NetMessage) == 29, "NetMessage must be 29 bytes");
