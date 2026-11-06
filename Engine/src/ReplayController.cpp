#include "ReplayController.h"
#include <cmath>
#include <stdexcept>
#include <utility>

ReplayController::ReplayController(EventManager &events)
    : events(events), simulationTime(events.now())
{
    subscriptions.push_back(events.subscribe(EventTypes::ReplayStart, [this](const Event &)
                                             { startRecording(); }));
    subscriptions.push_back(events.subscribe(EventTypes::ReplayStop, [this](const Event &)
                                             { stop(); }));
    subscriptions.push_back(events.subscribe(EventTypes::ReplayPlayback, [this](const Event &)
                                             { startPlayback(); }));
    subscriptions.push_back(events.subscribeAll([this](const Event &event)
                                                { recordInput(event); }));
    events.setTime(simulationTime);
}

ReplayController::~ReplayController()
{
    for (auto subscription : subscriptions)
        events.unsubscribe(subscription);
    events.setPlaybackMode(false);
    events.resumeTime();
}

void ReplayController::setStateCallbacks(CaptureState capture, RestoreState restore)
{
    if (recording || playing || frameOpen)
        throw std::logic_error("Replay state callbacks can only change between sessions");
    if (!capture || !restore)
        throw std::invalid_argument("Replay requires capture and restore callbacks");
    this->capture = std::move(capture);
    this->restore = std::move(restore);
}

void ReplayController::startRecording()
{
    commands.push_back(Command::Record);
}

void ReplayController::stop()
{
    commands.push_back(Command::Stop);
}

void ReplayController::startPlayback()
{
    commands.push_back(Command::Play);
}

void ReplayController::finishPlayback()
{
    playing = false;
    events.setPlaybackMode(false);
}

void ReplayController::apply(Command command)
{
    if (command == Command::Stop)
    {
        recording = false;
        finishPlayback();
        return;
    }
    if (command == Command::Record)
    {
        if (recording || playing)
            return;
        if (!capture || !restore)
            throw std::logic_error("Configure replay state callbacks before recording");
        auto state = capture();
        auto pending = events.getPendingEvents();
        clip = {std::move(state), simulationTime, {}, {}};
        for (auto event : pending)
            if (event.type != EventTypes::Input && !isReplayControl(event.type))
            {
                event.timestamp -= simulationTime;
                clip.pendingEvents.push_back(std::move(event));
            }
        recording = true;
        return;
    }
    if (recording || playing || clip.frames.empty())
        return;
    restore(clip.initialState);
    events.setPlaybackMode(true);
    events.clear();
    playbackIndex = 0;
    simulationTime = clip.startTime;
    playing = true;
    for (auto event : clip.pendingEvents)
    {
        event.timestamp += clip.startTime;
        event.origin = EventOrigin::Replay;
        event.shareable = false;
        events.raise(std::move(event));
    }
}

void ReplayController::beginFrame(const FrameTiming &timing)
{
    if (frameOpen)
        throw std::logic_error("Finish the current replay frame before starting another");
    if (!std::isfinite(timing.realDelta) || timing.realDelta < 0 || timing.timelines.empty())
        throw std::invalid_argument("Replay frame timing is invalid");
    for (const auto &timeline : timing.timelines)
        if (!std::isfinite(timeline.delta) || timeline.delta < 0 ||
            !std::isfinite(timeline.scale) || timeline.scale < 0)
            throw std::invalid_argument("Replay timeline timing is invalid");
    while (!commands.empty())
    {
        auto command = commands.front();
        commands.pop_front();
        apply(command);
    }
    currentFrame = {};
    currentFrame.timing = timing;
    phase = ReplayPhase::Input;
    lastPhase = -1;
    if (playing)
    {
        if (clip.frames[playbackIndex].timing.timelines.size() != timing.timelines.size())
        {
            finishPlayback();
            throw std::logic_error("Replay requires the recorded timeline layout");
        }
        simulationTime = clip.startTime + clip.frames[playbackIndex].offset;
    }
    else
        simulationTime += static_cast<EventTime>(std::llround(timing.timelines.front().delta * 1'000'000.0));
    if (recording)
    {
        if (!clip.frames.empty() && clip.frames.front().timing.timelines.size() != timing.timelines.size())
            throw std::logic_error("Timeline layout cannot change during recording");
        currentFrame.offset = simulationTime - clip.startTime;
    }
    events.setTime(simulationTime);
    frameOpen = true;
}

const FrameTiming &ReplayController::getFrameTiming() const
{
    return playing ? clip.frames[playbackIndex].timing : currentFrame.timing;
}

std::size_t ReplayController::dispatch(ReplayPhase nextPhase, std::size_t budget)
{
    if (!frameOpen || static_cast<std::size_t>(nextPhase) >= currentFrame.inputs.size())
        throw std::logic_error("Replay dispatch requires an open frame and valid phase");
    if (static_cast<int>(nextPhase) <= lastPhase)
        throw std::logic_error("Replay phases must run in frame order");
    phase = nextPhase;
    lastPhase = static_cast<int>(nextPhase);
    const auto index = static_cast<std::size_t>(phase);
    if (playing)
        for (auto event : clip.frames[playbackIndex].inputs[index])
        {
            event.timestamp += clip.startTime;
            event.origin = EventOrigin::Replay;
            event.shareable = false;
            events.raise(std::move(event));
        }
    return events.dispatch(budget);
}

void ReplayController::recordInput(const Event &event)
{
    if (!recording || !frameOpen || event.type != EventTypes::Input || event.origin != EventOrigin::Local)
        return;
    auto recorded = event;
    recorded.timestamp -= clip.startTime;
    currentFrame.inputs[static_cast<std::size_t>(phase)].push_back(std::move(recorded));
}

void ReplayController::endFrame()
{
    if (!frameOpen)
        throw std::logic_error("No replay frame is open");
    if (lastPhase != static_cast<int>(ReplayPhase::Workers))
        throw std::logic_error("Dispatch all replay phases before ending the frame");
    if (recording)
        clip.frames.push_back(std::move(currentFrame));
    if (playing && ++playbackIndex == clip.frames.size())
        finishPlayback();
    frameOpen = false;
}
