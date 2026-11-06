#pragma once
#include "EventManager.h"
#include <any>
#include <array>
#include <deque>
#include <functional>
#include <vector>

struct TimelineFrame
{
    float delta = 0;
    double scale = 1;
    bool paused = false;
};

struct FrameTiming
{
    float realDelta = 0;
    std::vector<TimelineFrame> timelines;
};

enum class ReplayPhase : std::size_t
{
    Input,
    Update,
    Workers
};

struct ReplayFrame
{
    FrameTiming timing;
    EventTime offset = 0;
    std::array<std::vector<Event>, 3> inputs;
};

struct ReplayRecording
{
    std::any initialState;
    EventTime startTime = 0;
    std::vector<Event> pendingEvents;
    std::vector<ReplayFrame> frames;
};

class ReplayController
{
public:
    using CaptureState = std::function<std::any()>;
    using RestoreState = std::function<void(const std::any &)>;

    explicit ReplayController(EventManager &events);
    ~ReplayController();
    ReplayController(const ReplayController &) = delete;
    ReplayController &operator=(const ReplayController &) = delete;

    void setStateCallbacks(CaptureState capture, RestoreState restore);
    void startRecording();
    void stop();
    void startPlayback();
    void beginFrame(const FrameTiming &timing);
    std::size_t dispatch(ReplayPhase phase, std::size_t budget = EventManager::DefaultBudget);
    void endFrame();

    bool isRecording() const { return recording; }
    bool isPlaying() const { return playing; }
    const ReplayRecording &getRecording() const { return clip; }
    const FrameTiming &getFrameTiming() const;

private:
    enum class Command { Record, Stop, Play };
    void apply(Command command);
    void recordInput(const Event &event);
    void finishPlayback();

    EventManager &events;
    std::vector<EventManager::Subscription> subscriptions;
    CaptureState capture;
    RestoreState restore;
    ReplayRecording clip;
    ReplayFrame currentFrame;
    std::deque<Command> commands;
    ReplayPhase phase = ReplayPhase::Input;
    int lastPhase = -1;
    EventTime simulationTime;
    std::size_t playbackIndex = 0;
    bool frameOpen = false;
    bool recording = false;
    bool playing = false;
};
