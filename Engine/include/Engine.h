#pragma once
#include <functional>
#include "Window.h"
#include "Renderer.h"
#include "EntityManager.h"
#include "Physics.h"
#include "Input.h"
#include "Collision.h"
class Engine
{
public:
    Engine(const std::string &title, int width = 1920, int height = 1080);

    EntityManager &getEntities() { return entities; }
    Physics &getPhysics() { return physics; }
    Renderer &getRenderer() { return renderer; }
    Window &getWindow() { return window; }

    void run(const std::function<void(float)> &gameUpdate);

private:
    bool initialized;
    Window window;
    Renderer renderer;
    EntityManager entities;
    Physics physics;
    bool running = true;
};
