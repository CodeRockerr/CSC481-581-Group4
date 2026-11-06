#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>

using EventType = uint32_t;
using EventTime = int64_t;

namespace EventTypes
{
    constexpr EventType Collision = 1;
    constexpr EventType Death = 2;
    constexpr EventType Spawn = 3;
    constexpr EventType Input = 4;
    constexpr EventType ReplayStart = 5;
    constexpr EventType ReplayStop = 6;
    constexpr EventType ReplayPlayback = 7;
    constexpr EventType FirstGame = 100;
}

enum class EventPriority : uint8_t
{
    Low,
    Normal,
    High,
    Critical
};

enum class EventOrigin : uint8_t
{
    Local,
    Network,
    Replay
};

struct EventId
{
    uint32_t ownerId = 0;
    uint64_t sequence = 0;

    bool operator==(const EventId &other) const
    {
        return ownerId == other.ownerId && sequence == other.sequence;
    }
};

struct EventObject
{
    uint32_t ownerId = 0;
    uint32_t netId = 0;

    bool operator==(const EventObject &other) const
    {
        return ownerId == other.ownerId && netId == other.netId;
    }
};

struct CollisionEvent
{
    float normalX = 0;
    float normalY = 0;
    bool trigger = false;
};

struct DeathEvent
{
    std::string reason;
};

struct SpawnEvent
{
    std::string spawnPoint;
};

struct InputEvent
{
    std::string action;
    float value = 0;
};

using EventValue = std::variant<int64_t, double, bool, std::string>;
using EventFields = std::map<std::string, EventValue>;
using EventPayload = std::variant<std::monostate, CollisionEvent, DeathEvent, SpawnEvent, InputEvent, EventFields>;

struct Event
{
    EventType type = 0;
    EventTime timestamp = -1;
    EventPriority priority = EventPriority::Normal;
    EventId id;
    EventObject source;
    EventObject target;
    EventPayload payload;
    EventOrigin origin = EventOrigin::Local;
    bool shareable = false;
};

inline bool isReplayControl(EventType type)
{
    return type == EventTypes::ReplayStart || type == EventTypes::ReplayStop || type == EventTypes::ReplayPlayback;
}
