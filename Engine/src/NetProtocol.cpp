#include "NetProtocol.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    constexpr uint32_t kMaxObjects = 1u << 20;

    void encodeBinary(const Packet &packet, std::string &out)
    {
        PacketHeader header{};
        header.format = static_cast<uint8_t>(WireFormat::Binary);
        header.type = static_cast<uint8_t>(packet.type);
        header.sender = packet.sender;
        header.tic = packet.tic;
        header.aux = packet.aux;
        header.count = static_cast<uint32_t>(packet.objects.size());

        const std::size_t body = packet.objects.size() * sizeof(ObjectState);
        out.resize(sizeof(PacketHeader) + body);
        std::memcpy(&out[0], &header, sizeof(PacketHeader));
        if (body != 0)
        {
            std::memcpy(&out[sizeof(PacketHeader)], packet.objects.data(), body);
        }
    }

    bool decodeBinary(const char *data, std::size_t size, Packet &out)
    {
        if (size < sizeof(PacketHeader))
        {
            return false;
        }
        PacketHeader header{};
        std::memcpy(&header, data, sizeof(PacketHeader));
        if (header.count > kMaxObjects ||
            size != sizeof(PacketHeader) + static_cast<std::size_t>(header.count) * sizeof(ObjectState))
        {
            return false;
        }
        out.type = static_cast<PacketType>(header.type);
        out.sender = header.sender;
        out.tic = header.tic;
        out.aux = header.aux;
        out.objects.resize(header.count);
        if (header.count != 0)
        {
            std::memcpy(out.objects.data(), data + sizeof(PacketHeader), header.count * sizeof(ObjectState));
        }
        return true;
    }

    void encodeText(const Packet &packet, std::string &out)
    {
        out.clear();
        out.reserve(48 + packet.objects.size() * 72);
        char line[256];
        int n = std::snprintf(line, sizeof(line), "T %u %u %lld %u %zu\n",
                              static_cast<unsigned>(packet.type), packet.sender,
                              static_cast<long long>(packet.tic), packet.aux, packet.objects.size());
        out.append(line, static_cast<std::size_t>(n));
        for (const ObjectState &s : packet.objects)
        {
            n = std::snprintf(line, sizeof(line), "%u %u %u %u %u %.3f %.3f %.3f %.3f %.3f %.3f\n",
                              s.netId, s.owner, static_cast<unsigned>(s.kind), static_cast<unsigned>(s.flags),
                              static_cast<unsigned>(s.frame), s.x, s.y, s.width, s.height, s.velocityX,
                              s.velocityY);
            out.append(line, static_cast<std::size_t>(n));
        }
    }

    struct Cursor
    {
        const char *p;
        const char *end;

        void skipSpaces()
        {
            while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t'))
            {
                ++p;
            }
        }

        bool token(char *buffer, std::size_t capacity)
        {
            skipSpaces();
            std::size_t n = 0;
            while (p < end && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t')
            {
                if (n + 1 >= capacity)
                {
                    return false;
                }
                buffer[n++] = *p++;
            }
            buffer[n] = '\0';
            return n != 0;
        }

        bool readUnsigned(unsigned long long &value)
        {
            char buffer[32];
            if (!token(buffer, sizeof(buffer)))
            {
                return false;
            }
            char *stop = nullptr;
            errno = 0;
            value = std::strtoull(buffer, &stop, 10);
            return errno == 0 && *stop == '\0';
        }

        bool readSigned(long long &value)
        {
            char buffer[32];
            if (!token(buffer, sizeof(buffer)))
            {
                return false;
            }
            char *stop = nullptr;
            errno = 0;
            value = std::strtoll(buffer, &stop, 10);
            return errno == 0 && *stop == '\0';
        }

        bool readFloat(float &value)
        {
            char buffer[48];
            if (!token(buffer, sizeof(buffer)))
            {
                return false;
            }
            char *stop = nullptr;
            value = std::strtof(buffer, &stop);
            return *stop == '\0';
        }
    };

    bool decodeText(const char *data, std::size_t size, Packet &out)
    {
        Cursor c{data + 1, data + size};
        unsigned long long type = 0, sender = 0, aux = 0, count = 0;
        long long tic = 0;
        if (!c.readUnsigned(type) || !c.readUnsigned(sender) || !c.readSigned(tic) || !c.readUnsigned(aux) ||
            !c.readUnsigned(count) || count > kMaxObjects)
        {
            return false;
        }
        out.type = static_cast<PacketType>(type);
        out.sender = static_cast<uint32_t>(sender);
        out.tic = tic;
        out.aux = static_cast<uint32_t>(aux);
        out.objects.resize(static_cast<std::size_t>(count));
        for (ObjectState &s : out.objects)
        {
            unsigned long long netId = 0, owner = 0, kind = 0, flags = 0, frame = 0;
            if (!c.readUnsigned(netId) || !c.readUnsigned(owner) || !c.readUnsigned(kind) ||
                !c.readUnsigned(flags) || !c.readUnsigned(frame) || !c.readFloat(s.x) || !c.readFloat(s.y) ||
                !c.readFloat(s.width) || !c.readFloat(s.height) || !c.readFloat(s.velocityX) ||
                !c.readFloat(s.velocityY))
            {
                return false;
            }
            s.netId = static_cast<uint32_t>(netId);
            s.owner = static_cast<uint32_t>(owner);
            s.kind = static_cast<uint16_t>(kind);
            s.flags = static_cast<uint8_t>(flags);
            s.frame = static_cast<uint8_t>(frame);
        }
        return true;
    }
}

namespace NetProtocol
{
    void encode(WireFormat format, const Packet &packet, std::string &out)
    {
        if (format == WireFormat::Text)
        {
            encodeText(packet, out);
        }
        else
        {
            encodeBinary(packet, out);
        }
    }

    std::string encode(WireFormat format, const Packet &packet)
    {
        std::string out;
        encode(format, packet, out);
        return out;
    }

    bool decode(const void *data, std::size_t size, Packet &out)
    {
        if (data == nullptr || size == 0)
        {
            return false;
        }
        const char *bytes = static_cast<const char *>(data);
        if (bytes[0] == static_cast<char>(WireFormat::Binary))
        {
            return decodeBinary(bytes, size, out);
        }
        if (bytes[0] == static_cast<char>(WireFormat::Text))
        {
            return decodeText(bytes, size, out);
        }
        return false;
    }

    bool parseFormat(const std::string &text, WireFormat &out)
    {
        if (text == "binary" || text == "b" || text == "B")
        {
            out = WireFormat::Binary;
            return true;
        }
        if (text == "text" || text == "t" || text == "T")
        {
            out = WireFormat::Text;
            return true;
        }
        return false;
    }

    const char *formatName(WireFormat format)
    {
        return format == WireFormat::Text ? "text" : "binary";
    }
}
