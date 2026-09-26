#include "Timeline.h"

Timeline::Timeline()
    : anchor(nullptr), ticSize(1), scale(1.0), startNS(SDL_GetTicksNS())
{
}

Timeline::Timeline(Timeline *anchor, int64_t ticSize, double scale)
    : anchor(anchor), ticSize(ticSize > 0 ? ticSize : 1), scale(scale < 0.0 ? 0.0 : scale)
{
    if (anchor)
        lastAnchorNS = anchor->nanoseconds();
    else
        startNS = SDL_GetTicksNS();
}

double Timeline::nanoseconds()
{
    std::lock_guard<std::mutex> lock(mutex);
    return nanosecondsLocked();
}

double Timeline::nanosecondsLocked()
{
    if (isGlobal())
        return static_cast<double>(SDL_GetTicksNS() - startNS);
    advance();
    return elapsedNS;
}

void Timeline::advance()
{
    double now = anchor->nanoseconds();
    if (!paused)
        elapsedNS += (now - lastAnchorNS) * scale;
    lastAnchorNS = now;
}

int64_t Timeline::getTime()
{
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int64_t>(nanosecondsLocked()) / ticSize;
}

double Timeline::getSeconds()
{
    return nanoseconds() / 1'000'000'000.0;
}

double Timeline::step()
{
    std::lock_guard<std::mutex> lock(mutex);
    double now = nanosecondsLocked();
    lastDelta = (now - lastStepNS) / 1'000'000'000.0;
    lastStepNS = now;
    return lastDelta;
}

double Timeline::getLastDelta() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return lastDelta;
}

void Timeline::pause()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (isGlobal() || paused)
        return;
    advance();
    paused = true;
}

void Timeline::unpause()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (isGlobal() || !paused)
        return;
    advance();
    paused = false;
}

bool Timeline::isPaused() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return paused;
}

void Timeline::setScale(double value)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (isGlobal())
        return;
    advance();
    scale = value < 0.0 ? 0.0 : value;
}

double Timeline::getScale() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return scale;
}

void Timeline::setTicSize(int64_t value)
{
    std::lock_guard<std::mutex> lock(mutex);
    ticSize = value > 0 ? value : 1;
}

int64_t Timeline::getTicSize() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return ticSize;
}
