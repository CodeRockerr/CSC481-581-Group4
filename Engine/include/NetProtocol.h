#pragma once
// Milestone 3 wire protocol: replicates game objects instead of fixed player/platform records.
//
// A packet is one 0MQ frame. Its first byte names the encoding:
//   'B' - WireFormat::Binary: packed little-endian structs (PacketHeader + ObjectState[count])
//   'T' - WireFormat::Text:   human-readable lines, one header line then one line per object
// Both encodings carry exactly the same information, so gameplay is identical and only the
// representation differs. This is the "second data format" for the Part 1C experiments.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class WireFormat : uint8_t
{
    Binary = 'B',
    Text = 'T',
};

enum class PacketType : uint8_t
{
    Join = 1,       // client -> server (or peer -> peer) handshake request
    JoinAccept = 2, // reply: sender = assigned id, aux = dedicated port
    Leave = 3,      // graceful disconnect, or a rejection
    State = 4,      // objects owned by the sender
    Snapshot = 5,   // every replicated object the server knows about
};

// Object kinds are game-defined tags that tell a receiver which components to build when a
// new replica appears. Engine-reserved values are below 100; games use 100 and up.
namespace NetKind
{
    constexpr uint16_t Unknown = 0;
    constexpr uint16_t Player = 1;
    constexpr uint16_t Platform = 2;
    constexpr uint16_t FirstGameKind = 100;
}

namespace NetFlags
{
    constexpr uint8_t FlipHorizontal = 1 << 0;
    constexpr uint8_t Visible = 1 << 1;
    constexpr uint8_t Active = 1 << 2;
}

#pragma pack(push, 1)

// The synchronized slice of one game object: identity plus the Transform, PhysicsBody velocity
// and Sprite animation fields. (owner, netId) is unique across the whole session.
struct ObjectState
{
    uint32_t netId = 0;
    uint32_t owner = 0; // 0 = server, otherwise client id (or peer port)
    uint16_t kind = NetKind::Unknown;
    uint8_t flags = NetFlags::Visible | NetFlags::Active;
    uint8_t frame = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float velocityX = 0.0f;
    float velocityY = 0.0f;
};

struct PacketHeader
{
    uint8_t format = static_cast<uint8_t>(WireFormat::Binary);
    uint8_t type = static_cast<uint8_t>(PacketType::State);
    uint32_t sender = 0;
    int64_t tic = 0;
    uint32_t aux = 0;
    uint32_t count = 0;
};

#pragma pack(pop)

static_assert(sizeof(ObjectState) == 36, "ObjectState must stay 36 bytes on the wire");
static_assert(sizeof(PacketHeader) == 22, "PacketHeader must stay 22 bytes on the wire");

inline uint64_t netKey(uint32_t owner, uint32_t netId)
{
    return (static_cast<uint64_t>(owner) << 32) | netId;
}

struct Packet
{
    PacketType type = PacketType::State;
    uint32_t sender = 0;
    int64_t tic = 0;
    uint32_t aux = 0;
    std::vector<ObjectState> objects;
};

namespace NetProtocol
{
    void encode(WireFormat format, const Packet &packet, std::string &out);
    std::string encode(WireFormat format, const Packet &packet);

    // Detects the format from the first byte. Returns false on any malformed input.
    bool decode(const void *data, std::size_t size, Packet &out);

    // Parses "binary"/"b" or "text"/"t". Returns false for anything else.
    bool parseFormat(const std::string &text, WireFormat &out);
    const char *formatName(WireFormat format);
}
