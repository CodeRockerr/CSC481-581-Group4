#pragma once
#include <functional>
#include "Window.h"
#include "Renderer.h"
#include "EntityManager.h"
#include "Physics.h"
#include "Input.h"
#include "Collision.h"
// Ties together all Milestone 1 systems and owns the main game loop
// (Task 1). Individual games call Engine::run() and pass in a callback
// for their own per-frame game logic (movement, collision responses,
// etc.) so that game-specific code never has to touch SDL directly.
class Engine
{
public:
    Engine(const std::string &title, int width = 0, int height = 0);

    EntityManager &getEntities() { return entities; }
    Physics &getPhysics() { return physics; }
    Renderer &getRenderer() { return renderer; }
    Window &getWindow() { return window; }

    // gameUpdate is called once per frame with deltaTime (seconds),
    // AFTER input/physics have been updated but BEFORE rendering.
    // This is where individual games put their own logic (Tasks 2-5
    // of the Individual Game Requirements).
    void run(const std::function<void(float)> &gameUpdate);

private:
    bool initialized;
    Window window;
    Renderer renderer;
    EntityManager entities;
    Physics physics;
    bool running = true;
};
