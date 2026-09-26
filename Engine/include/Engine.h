#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "Window.h"
#include "Renderer.h"
#include "EntityManager.h"
#include "Physics.h"
#include "Input.h"
#include "Collision.h"
#include "Timeline.h"
class Engine
{
public:
    static constexpr int WorldTime = 0;
    static constexpr int PlayerTime = 1;
    static constexpr int64_t DefaultTicSize = 1'000'000;

    Engine(const std::string &title, int width = 1920, int height = 1080);

    EntityManager &getEntities() { return entities; }
    Physics &getPhysics() { return physics; }
    Renderer &getRenderer() { return renderer; }
    Window &getWindow() { return window; }

    Timeline &getGlobalTimeline() { return globalTimeline; }
    Timeline &getTimeline(int id) { return *timelines[id]; }
    int createTimeline(int64_t ticSize = DefaultTicSize, double scale = 1.0, int anchorId = -1);

    void run(const std::function<void(float)> &gameUpdate);

private:
    bool initialized;
    Window window;
    Renderer renderer;
    EntityManager entities;
    Physics physics;
    Timeline globalTimeline;
    std::vector<std::unique_ptr<Timeline>> timelines;
    bool running = true;

    void handleTimeKeys();
    std::vector<float> stepTimelines();
};
