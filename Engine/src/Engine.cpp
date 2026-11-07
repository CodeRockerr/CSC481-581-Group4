#include "Engine.h"
#include "InputSystem.h"
#include <stdexcept>

namespace
{
    bool initializeSDL()
    {
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }

        return true;
    }
}

Engine::Engine(const std::string &title, int width, int height)
    : initialized(initializeSDL()), window(title, width, height), renderer(window), physics(980.0f)
{
    entities.setReferenceResolution(window.getWidth(), window.getHeight());
    objects.setReferenceResolution(window.getWidth(), window.getHeight());
    camera.setViewport(static_cast<float>(window.getWidth()), static_cast<float>(window.getHeight()));
    createTimeline(DefaultTicSize, 1.0);
    createTimeline(DefaultTicSize, 1.0);
    events = std::make_unique<EventManager>(getTimeline(WorldTime));
    replay = std::make_unique<ReplayController>(*events);
}

int Engine::createTimeline(int64_t ticSize, double scale, int anchorId)
{
    Timeline *anchor = &globalTimeline;
    if (anchorId >= 0 && anchorId < static_cast<int>(timelines.size()))
    {
        anchor = timelines[anchorId].get();
    }
    timelines.push_back(std::make_unique<Timeline>(anchor, ticSize, scale));
    return static_cast<int>(timelines.size()) - 1;
}

void Engine::handleTimeKeys()
{
    Timeline &playerTime = getTimeline(PlayerTime);
    if (Input::isKeyJustPressed(SDL_SCANCODE_P))
    {
        if (playerTime.isPaused())
        {
            playerTime.unpause();
        }
        else
        {
            playerTime.pause();
        }
    }
    if (Input::isKeyJustPressed(SDL_SCANCODE_1))
    {
        playerTime.setScale(0.5);
    }
    if (Input::isKeyJustPressed(SDL_SCANCODE_2))
    {
        playerTime.setScale(1.0);
    }
    if (Input::isKeyJustPressed(SDL_SCANCODE_3))
    {
        playerTime.setScale(2.0);
    }
}

float Engine::getFrameDelta(int timelineId) const
{
    if (timelineId < 0 || timelineId >= static_cast<int>(frameTiming.timelines.size()))
        return 0;
    return frameTiming.timelines[timelineId].delta;
}

FrameTiming Engine::stepTimelines()
{
    globalTimeline.step();
    FrameTiming timing;
    timing.realDelta = static_cast<float>(globalTimeline.getLastDelta());
    for (auto &t : timelines)
    {
        const auto delta = static_cast<float>(t->step());
        const bool paused = t->isPaused();
        timing.timelines.push_back({paused ? 0.f : delta, t->getScale(), paused});
    }
    return timing;
}

void Engine::buildPlayerFrame()
{
    float deltaTime = getFrameDelta(PlayerTime);
    physics.updateTimeline(entities, PlayerTime, deltaTime);
    entities.updateTimeline(PlayerTime, deltaTime);
    physics.updateTimeline(objects, PlayerTime, deltaTime);
    objects.updateTimeline(PlayerTime, deltaTime);
}

void Engine::buildWorldFrame()
{
    for (int id = 0; id < static_cast<int>(timelines.size()); ++id)
    {
        if (id == PlayerTime)
        {
            continue;
        }
        float deltaTime = getFrameDelta(id);
        physics.updateTimeline(entities, id, deltaTime);
        entities.updateTimeline(id, deltaTime);
        physics.updateTimeline(objects, id, deltaTime);
        objects.updateTimeline(id, deltaTime);
    }

    if (worldCallback)
    {
        worldCallback(getFrameDelta(WorldTime));
    }
}

void Engine::paceFrame(Uint64 frameStartNS)
{
    constexpr Uint64 baseFrameNS = 1'000'000'000 / 60;
    double scale = getTimeline(PlayerTime).getScale();
    if (scale < 0.5)
    {
        scale = 0.5;
    }

    const Uint64 frameNS = static_cast<Uint64>(static_cast<double>(baseFrameNS) / scale);
    const Uint64 elapsed = SDL_GetTicksNS() - frameStartNS;
    if (elapsed < frameNS)
    {
        SDL_DelayNS(frameNS - elapsed);
    }
}

void Engine::run(const std::function<void(float)> &gameUpdate,
                 const std::function<void(float)> &worldUpdate)
{
    worldCallback = worldUpdate;
    frameTiming = stepTimelines();
    entities.copySnapshot(finishedFrame);
    objects.copySnapshot(finishedObjectFrame);

    playerWorker = std::make_unique<FrameWorker>([this]
                                                 { buildPlayerFrame(); });
    worldWorker = std::make_unique<FrameWorker>([this]
                                                { buildWorldFrame(); });

    while (running)
    {
        const Uint64 frameStartNS = SDL_GetTicksNS();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
                window.setSize(event.window.data1, event.window.data2);
                camera.setViewport(static_cast<float>(event.window.data1),
                                   static_cast<float>(event.window.data2));
            }
        }

        Input::update();
        if (!replay->isPlaying())
            handleTimeKeys();
        replay->beginFrame(stepTimelines());
        frameTiming = replay->getFrameTiming();
        if (replay->isPlaying())
            for (std::size_t i = 0; i < timelines.size(); ++i)
            {
                timelines[i]->setScale(frameTiming.timelines[i].scale);
                if (frameTiming.timelines[i].paused)
                    timelines[i]->pause();
                else
                    timelines[i]->unpause();
            }
        InputSystem::poll(*events, objects);
        replay->dispatch(ReplayPhase::Input);

        if (gameUpdate)
        {
            gameUpdate(getFrameDelta(WorldTime));
        }
        replay->dispatch(ReplayPhase::Update);

        playerWorker->start();
        worldWorker->start();

        renderer.clear();
        camera.update(objects);
        entities.drawSnapshot(finishedFrame, renderer.getHandle(), window.getWidth(), window.getHeight());
        objects.drawSnapshot(finishedObjectFrame, renderer.getHandle(), window.getWidth(), window.getHeight(), camera);

        playerWorker->wait();
        worldWorker->wait();

        replay->dispatch(ReplayPhase::Workers);
        replay->endFrame();
        objects.flushDestroyed();

        renderer.present();
        entities.copySnapshot(finishedFrame);
        objects.copySnapshot(finishedObjectFrame);
        paceFrame(frameStartNS);
    }
}
