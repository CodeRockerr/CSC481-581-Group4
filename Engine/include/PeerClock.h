#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

inline int64_t peerNowTic()
{
    using Clock = std::chrono::system_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

inline int64_t sharedAnchor(int64_t localStartTic, int64_t otherStartTic)
{
    return std::min(localStartTic, otherStartTic);
}

inline float peerPlatformX(int64_t nowTic, int64_t anchorTic)
{
    constexpr float centerX = 520.0f;
    constexpr float amplitude = 180.0f;
    const double seconds = static_cast<double>(nowTic - anchorTic) / 1000000.0;
    return centerX + amplitude * static_cast<float>(std::sin(seconds));
}

inline float peerPlatformY()
{
    return 520.0f;
}
