#include "EventCodec.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <variant>

namespace
{
    enum PayloadKind : uint8_t
    {
        KindNone = 0,
        KindCollision = 1,
        KindDeath = 2,
        KindSpawn = 3,
        KindInput = 4,
        KindFields = 5,
    };

    enum FieldTag : uint8_t
    {
        TagInt = 0,
        TagDouble = 1,
        TagBool = 2,
        TagString = 3,
    };

    uint8_t kindOf(const EventPayload &payload)
    {
        switch (payload.index())
        {
        case 1:
            return KindCollision;
        case 2:
            return KindDeath;
        case 3:
            return KindSpawn;
        case 4:
            return KindInput;
        case 5:
            return KindFields;
        default:
            return KindNone;
        }
    }

    // ---------------------------------------------------------------- binary

    template <typename T>
    void put(std::string &out, T value)
    {
        out.append(reinterpret_cast<const char *>(&value), sizeof(T));
    }

    void putString(std::string &out, const std::string &text)
    {
        const std::size_t n = text.size() > EventCodec::MaxStringBytes ? EventCodec::MaxStringBytes : text.size();
        put<uint32_t>(out, static_cast<uint32_t>(n));
        out.append(text.data(), n);
    }

    struct BinaryReader
    {
        const char *p;
        const char *end;

        template <typename T>
        bool get(T &value)
        {
            if (static_cast<std::size_t>(end - p) < sizeof(T))
            {
                return false;
            }
            std::memcpy(&value, p, sizeof(T));
            p += sizeof(T);
            return true;
        }

        bool getString(std::string &text)
        {
            uint32_t n = 0;
            if (!get(n) || n > EventCodec::MaxStringBytes || static_cast<std::size_t>(end - p) < n)
            {
                return false;
            }
            text.assign(p, n);
            p += n;
            return true;
        }
    };

    // ------------------------------------------------------------------ text

    // Strings are written as "~" + percent-encoding so that an empty string is still a token
    // and spaces/newlines can never split a field.
    void putTextString(std::string &out, const std::string &text)
    {
        static const char *hex = "0123456789ABCDEF";
        out.push_back('~');
        const std::size_t n = text.size() > EventCodec::MaxStringBytes ? EventCodec::MaxStringBytes : text.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                               c == '_' || c == '-' || c == '.';
            if (plain)
            {
                out.push_back(static_cast<char>(c));
            }
            else
            {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 15]);
            }
        }
    }

    void putTextNumber(std::string &out, const char *format, double value)
    {
        char buffer[64];
        const int n = std::snprintf(buffer, sizeof(buffer), format, value);
        out.append(buffer, static_cast<std::size_t>(n));
    }

    template <typename T>
    void putTextInteger(std::string &out, T value)
    {
        char buffer[32];
        int n = 0;
        if (std::is_signed<T>::value)
        {
            n = std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        }
        else
        {
            n = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
        }
        out.append(buffer, static_cast<std::size_t>(n));
    }

    int hexValue(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    struct TextReader
    {
        const char *p;
        const char *end;

        bool token(std::string &out)
        {
            while (p < end && (*p == ' ' || *p == '\t'))
            {
                ++p;
            }
            out.clear();
            while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            {
                out.push_back(*p++);
            }
            return !out.empty();
        }

        bool atEnd()
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
            {
                ++p;
            }
            return p == end;
        }

        bool unsignedValue(unsigned long long &value, unsigned long long max)
        {
            std::string t;
            if (!token(t))
            {
                return false;
            }
            char *stop = nullptr;
            errno = 0;
            value = std::strtoull(t.c_str(), &stop, 10);
            return errno == 0 && *stop == '\0' && value <= max && t[0] != '-';
        }

        bool signedValue(long long &value)
        {
            std::string t;
            if (!token(t))
            {
                return false;
            }
            char *stop = nullptr;
            errno = 0;
            value = std::strtoll(t.c_str(), &stop, 10);
            return errno == 0 && *stop == '\0';
        }

        bool doubleValue(double &value)
        {
            std::string t;
            if (!token(t))
            {
                return false;
            }
            char *stop = nullptr;
            value = std::strtod(t.c_str(), &stop);
            return *stop == '\0';
        }

        bool floatValue(float &value)
        {
            double d = 0;
            if (!doubleValue(d))
            {
                return false;
            }
            value = static_cast<float>(d);
            return true;
        }

        bool stringValue(std::string &out)
        {
            std::string t;
            if (!token(t) || t[0] != '~')
            {
                return false;
            }
            out.clear();
            for (std::size_t i = 1; i < t.size(); ++i)
            {
                if (t[i] == '%')
                {
                    if (i + 2 >= t.size())
                    {
                        return false;
                    }
                    const int hi = hexValue(t[i + 1]);
                    const int lo = hexValue(t[i + 2]);
                    if (hi < 0 || lo < 0)
                    {
                        return false;
                    }
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                }
                else
                {
                    out.push_back(t[i]);
                }
                if (out.size() > EventCodec::MaxStringBytes)
                {
                    return false;
                }
            }
            return true;
        }
    };

    bool validPriority(unsigned long long value)
    {
        return value <= static_cast<unsigned long long>(EventPriority::Critical);
    }
}

namespace EventCodec
{
    void encodeBinary(const Event &event, std::string &out)
    {
        put<uint32_t>(out, event.type);
        put<int64_t>(out, event.timestamp);
        put<uint8_t>(out, static_cast<uint8_t>(event.priority));
        put<uint32_t>(out, event.id.ownerId);
        put<uint64_t>(out, event.id.sequence);
        put<uint32_t>(out, event.source.ownerId);
        put<uint32_t>(out, event.source.netId);
        put<uint32_t>(out, event.target.ownerId);
        put<uint32_t>(out, event.target.netId);
        const uint8_t kind = kindOf(event.payload);
        put<uint8_t>(out, kind);

        switch (kind)
        {
        case KindCollision:
        {
            const CollisionEvent &c = std::get<CollisionEvent>(event.payload);
            put<float>(out, c.normalX);
            put<float>(out, c.normalY);
            put<uint8_t>(out, c.trigger ? 1 : 0);
            break;
        }
        case KindDeath:
            putString(out, std::get<DeathEvent>(event.payload).reason);
            break;
        case KindSpawn:
            putString(out, std::get<SpawnEvent>(event.payload).spawnPoint);
            break;
        case KindInput:
        {
            const InputEvent &i = std::get<InputEvent>(event.payload);
            putString(out, i.action);
            put<float>(out, i.value);
            break;
        }
        case KindFields:
        {
            const EventFields &fields = std::get<EventFields>(event.payload);
            const std::size_t n = fields.size() > MaxFields ? MaxFields : fields.size();
            put<uint32_t>(out, static_cast<uint32_t>(n));
            std::size_t written = 0;
            for (const auto &entry : fields)
            {
                if (written++ == n)
                {
                    break;
                }
                putString(out, entry.first);
                const EventValue &v = entry.second;
                switch (v.index())
                {
                case 0:
                    put<uint8_t>(out, TagInt);
                    put<int64_t>(out, std::get<int64_t>(v));
                    break;
                case 1:
                    put<uint8_t>(out, TagDouble);
                    put<double>(out, std::get<double>(v));
                    break;
                case 2:
                    put<uint8_t>(out, TagBool);
                    put<uint8_t>(out, std::get<bool>(v) ? 1 : 0);
                    break;
                default:
                    put<uint8_t>(out, TagString);
                    putString(out, std::get<std::string>(v));
                    break;
                }
            }
            break;
        }
        default:
            break;
        }
    }

    bool decodeBinary(const char *data, std::size_t size, Event &out)
    {
        BinaryReader r{data, data + size};
        Event event;
        uint8_t priority = 0, kind = 0;
        if (!r.get(event.type) || !r.get(event.timestamp) || !r.get(priority) || !r.get(event.id.ownerId) ||
            !r.get(event.id.sequence) || !r.get(event.source.ownerId) || !r.get(event.source.netId) ||
            !r.get(event.target.ownerId) || !r.get(event.target.netId) || !r.get(kind))
        {
            return false;
        }
        if (event.type == 0 || !validPriority(priority))
        {
            return false;
        }
        event.priority = static_cast<EventPriority>(priority);

        switch (kind)
        {
        case KindNone:
            break;
        case KindCollision:
        {
            CollisionEvent c;
            uint8_t trigger = 0;
            if (!r.get(c.normalX) || !r.get(c.normalY) || !r.get(trigger))
            {
                return false;
            }
            c.trigger = trigger != 0;
            event.payload = c;
            break;
        }
        case KindDeath:
        {
            DeathEvent d;
            if (!r.getString(d.reason))
            {
                return false;
            }
            event.payload = std::move(d);
            break;
        }
        case KindSpawn:
        {
            SpawnEvent s;
            if (!r.getString(s.spawnPoint))
            {
                return false;
            }
            event.payload = std::move(s);
            break;
        }
        case KindInput:
        {
            InputEvent i;
            if (!r.getString(i.action) || !r.get(i.value))
            {
                return false;
            }
            event.payload = std::move(i);
            break;
        }
        case KindFields:
        {
            uint32_t n = 0;
            if (!r.get(n) || n > MaxFields)
            {
                return false;
            }
            EventFields fields;
            for (uint32_t k = 0; k < n; ++k)
            {
                std::string name;
                uint8_t tag = 0;
                if (!r.getString(name) || !r.get(tag))
                {
                    return false;
                }
                switch (tag)
                {
                case TagInt:
                {
                    int64_t v = 0;
                    if (!r.get(v))
                        return false;
                    fields[name] = v;
                    break;
                }
                case TagDouble:
                {
                    double v = 0;
                    if (!r.get(v))
                        return false;
                    fields[name] = v;
                    break;
                }
                case TagBool:
                {
                    uint8_t v = 0;
                    if (!r.get(v))
                        return false;
                    fields[name] = (v != 0);
                    break;
                }
                case TagString:
                {
                    std::string v;
                    if (!r.getString(v))
                        return false;
                    fields[name] = std::move(v);
                    break;
                }
                default:
                    return false;
                }
            }
            event.payload = std::move(fields);
            break;
        }
        default:
            return false;
        }

        if (r.p != r.end)
        {
            return false;
        }
        out = std::move(event);
        return true;
    }

    void encodeText(const Event &event, std::string &out)
    {
        putTextInteger(out, event.type);
        out.push_back(' ');
        putTextInteger(out, event.timestamp);
        out.push_back(' ');
        putTextInteger(out, static_cast<unsigned>(event.priority));
        out.push_back(' ');
        putTextInteger(out, event.id.ownerId);
        out.push_back(' ');
        putTextInteger(out, event.id.sequence);
        out.push_back(' ');
        putTextInteger(out, event.source.ownerId);
        out.push_back(' ');
        putTextInteger(out, event.source.netId);
        out.push_back(' ');
        putTextInteger(out, event.target.ownerId);
        out.push_back(' ');
        putTextInteger(out, event.target.netId);
        out.push_back(' ');
        const uint8_t kind = kindOf(event.payload);
        putTextInteger(out, static_cast<unsigned>(kind));

        switch (kind)
        {
        case KindCollision:
        {
            const CollisionEvent &c = std::get<CollisionEvent>(event.payload);
            out.push_back(' ');
            putTextNumber(out, "%.9g", c.normalX);
            out.push_back(' ');
            putTextNumber(out, "%.9g", c.normalY);
            out.push_back(' ');
            out.push_back(c.trigger ? '1' : '0');
            break;
        }
        case KindDeath:
            out.push_back(' ');
            putTextString(out, std::get<DeathEvent>(event.payload).reason);
            break;
        case KindSpawn:
            out.push_back(' ');
            putTextString(out, std::get<SpawnEvent>(event.payload).spawnPoint);
            break;
        case KindInput:
        {
            const InputEvent &i = std::get<InputEvent>(event.payload);
            out.push_back(' ');
            putTextString(out, i.action);
            out.push_back(' ');
            putTextNumber(out, "%.9g", i.value);
            break;
        }
        case KindFields:
        {
            const EventFields &fields = std::get<EventFields>(event.payload);
            const std::size_t n = fields.size() > MaxFields ? MaxFields : fields.size();
            out.push_back(' ');
            putTextInteger(out, static_cast<unsigned>(n));
            std::size_t written = 0;
            for (const auto &entry : fields)
            {
                if (written++ == n)
                {
                    break;
                }
                out.push_back(' ');
                putTextString(out, entry.first);
                out.push_back(' ');
                const EventValue &v = entry.second;
                switch (v.index())
                {
                case 0:
                    out += "i ";
                    putTextInteger(out, std::get<int64_t>(v));
                    break;
                case 1:
                    out += "d ";
                    putTextNumber(out, "%.17g", std::get<double>(v));
                    break;
                case 2:
                    out += "b ";
                    out.push_back(std::get<bool>(v) ? '1' : '0');
                    break;
                default:
                    out += "s ";
                    putTextString(out, std::get<std::string>(v));
                    break;
                }
            }
            break;
        }
        default:
            break;
        }
    }

    bool decodeText(const char *begin, const char *end, Event &out)
    {
        TextReader r{begin, end};
        Event event;
        unsigned long long type = 0, priority = 0, idOwner = 0, idSeq = 0, srcOwner = 0, srcNet = 0, tgtOwner = 0,
                           tgtNet = 0, kind = 0;
        long long timestamp = 0;
        if (!r.unsignedValue(type, UINT32_MAX) || !r.signedValue(timestamp) || !r.unsignedValue(priority, 255) ||
            !r.unsignedValue(idOwner, UINT32_MAX) || !r.unsignedValue(idSeq, UINT64_MAX) ||
            !r.unsignedValue(srcOwner, UINT32_MAX) || !r.unsignedValue(srcNet, UINT32_MAX) ||
            !r.unsignedValue(tgtOwner, UINT32_MAX) || !r.unsignedValue(tgtNet, UINT32_MAX) ||
            !r.unsignedValue(kind, 255))
        {
            return false;
        }
        if (type == 0 || !validPriority(priority))
        {
            return false;
        }
        event.type = static_cast<EventType>(type);
        event.timestamp = timestamp;
        event.priority = static_cast<EventPriority>(priority);
        event.id = {static_cast<uint32_t>(idOwner), idSeq};
        event.source = {static_cast<uint32_t>(srcOwner), static_cast<uint32_t>(srcNet)};
        event.target = {static_cast<uint32_t>(tgtOwner), static_cast<uint32_t>(tgtNet)};

        switch (kind)
        {
        case KindNone:
            break;
        case KindCollision:
        {
            CollisionEvent c;
            unsigned long long trigger = 0;
            if (!r.floatValue(c.normalX) || !r.floatValue(c.normalY) || !r.unsignedValue(trigger, 1))
            {
                return false;
            }
            c.trigger = trigger != 0;
            event.payload = c;
            break;
        }
        case KindDeath:
        {
            DeathEvent d;
            if (!r.stringValue(d.reason))
                return false;
            event.payload = std::move(d);
            break;
        }
        case KindSpawn:
        {
            SpawnEvent s;
            if (!r.stringValue(s.spawnPoint))
                return false;
            event.payload = std::move(s);
            break;
        }
        case KindInput:
        {
            InputEvent i;
            if (!r.stringValue(i.action) || !r.floatValue(i.value))
                return false;
            event.payload = std::move(i);
            break;
        }
        case KindFields:
        {
            unsigned long long n = 0;
            if (!r.unsignedValue(n, MaxFields))
                return false;
            EventFields fields;
            for (unsigned long long k = 0; k < n; ++k)
            {
                std::string name, tag;
                if (!r.stringValue(name) || !r.token(tag) || tag.size() != 1)
                    return false;
                switch (tag[0])
                {
                case 'i':
                {
                    long long v = 0;
                    if (!r.signedValue(v))
                        return false;
                    fields[name] = static_cast<int64_t>(v);
                    break;
                }
                case 'd':
                {
                    double v = 0;
                    if (!r.doubleValue(v))
                        return false;
                    fields[name] = v;
                    break;
                }
                case 'b':
                {
                    unsigned long long v = 0;
                    if (!r.unsignedValue(v, 1))
                        return false;
                    fields[name] = (v != 0);
                    break;
                }
                case 's':
                {
                    std::string v;
                    if (!r.stringValue(v))
                        return false;
                    fields[name] = std::move(v);
                    break;
                }
                default:
                    return false;
                }
            }
            event.payload = std::move(fields);
            break;
        }
        default:
            return false;
        }

        if (!r.atEnd())
        {
            return false;
        }
        out = std::move(event);
        return true;
    }
}
