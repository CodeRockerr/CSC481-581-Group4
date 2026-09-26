#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <mutex>

class Timeline
{
public:
    Timeline();
    Timeline(Timeline *anchor, int64_t ticSize, double scale = 1.0);

    int64_t getTime();
    double getSeconds();

    double step();
    double getLastDelta() const;

    void pause();
    void unpause();
    bool isPaused() const;

    void setScale(double value);
    double getScale() const;

    void setTicSize(int64_t value);
    int64_t getTicSize() const;

    bool isGlobal() const { return anchor == nullptr; }

private:
    double nanoseconds();
    double nanosecondsLocked();
    void advance();

    Timeline *anchor;
    int64_t ticSize;
    double scale;
    bool paused = false;
    Uint64 startNS = 0;
    double lastAnchorNS = 0.0;
    double elapsedNS = 0.0;
    double lastStepNS = 0.0;
    double lastDelta = 0.0;
    mutable std::mutex mutex;
};
