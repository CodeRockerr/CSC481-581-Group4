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

class EntityManager
{
public:
    Entity *createEntity(float x, float y, float w, float h,
                         SDL_Color color = {255, 255, 255, 255});

    void setTexture(Entity *entity, SDL_Texture *texture, int frameCount = 1);

    void drawAll(SDL_Renderer *renderer, int windowWidth, int windowHeight) const;
    void drawSnapshot(const std::vector<Entity> &snapshot, SDL_Renderer *renderer, int windowWidth, int windowHeight) const;
    void copySnapshot(std::vector<Entity> &snapshot) const;

    void updateAll(float deltaTime);
    void updateAll(const std::vector<float> &timelineDeltas);
    void updateTimeline(int timelineId, float deltaTime);

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
    void drawEntity(const Entity &e, SDL_Renderer *renderer, float scaleX, float scaleY) const;

    std::vector<std::unique_ptr<Entity>> entities;
    ScaleMode scaleMode = ScaleMode::Pixel;
    int referenceWidth = 1280;
    int referenceHeight = 800;
};
