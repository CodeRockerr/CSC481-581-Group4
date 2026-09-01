#pragma once
#include <vector>
#include <memory>
#include <SDL3/SDL.h>
#include "Entity.h"

enum class ScaleMode
{
    Pixel,
    Percentage
};

// Task 2 (cont.): the manager side of the generic entity system.
class EntityManager
{
public:
    Entity *createEntity(float x, float y, float w, float h,
                         SDL_Color color = {255, 255, 255, 255});

    // windowWidth/windowHeight are the CURRENT window size (may change if
    // resized). Only used when scaleMode == Percentage.
    void drawAll(SDL_Renderer *renderer, int windowWidth, int windowHeight) const;

    void updateAll(float deltaTime);

    // Task 6: Scaling system
    // referenceWidth/Height is the "design resolution" entities were
    // placed at, captured once at startup. Percentage mode scales
    // everything relative to how much the window has grown/shrunk from
    // that reference size.
    void setReferenceResolution(int w, int h)
    {
        referenceWidth = w;
        referenceHeight = h;
    }
    void setScaleMode(ScaleMode mode) { scaleMode = mode; }
    void toggleScaleMode()
    {
        scaleMode = (scaleMode == ScaleMode::Pixel) ? ScaleMode::Percentage : ScaleMode::Pixel;
    }
    ScaleMode getScaleMode() const { return scaleMode; }

    const std::vector<std::unique_ptr<Entity>> &getEntities() const { return entities; }

private:
    std::vector<std::unique_ptr<Entity>> entities;
    ScaleMode scaleMode = ScaleMode::Pixel;
    int referenceWidth = 1280;
    int referenceHeight = 800;
};