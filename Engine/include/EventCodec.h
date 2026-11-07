#pragma once
// Explicit wire encoding for Event (Milestone 4 network events).
//
// Raw copying of an Event is not a wire format (strings, maps, variants), so every field
// is written explicitly. Two formats mirror NetProtocol's WireFormat:
//   Binary: compact little-endian-as-host records (same convention as ObjectState).
//   Text:   one human-readable line per event, strings percent-encoded.
//
// `origin` and `shareable` are NOT transmitted. The receiver decides them: anything that
// arrives from the network is Origin::Network and is never re-published.
#include <cstddef>
#include <string>

#include "Event.h"

namespace EventCodec
{
    constexpr std::size_t MaxStringBytes = 4096;
    constexpr std::size_t MaxFields = 256;

    // Both encoders APPEND to `out`.
    void encodeBinary(const Event &event, std::string &out);
    void encodeText(const Event &event, std::string &out);

    // Decoders require the buffer to hold exactly one event. Return false on any malformed
    // input (truncation, unknown payload kind, oversize string, trailing bytes).
    bool decodeBinary(const char *data, std::size_t size, Event &out);
    bool decodeText(const char *begin, const char *end, Event &out);
}
