#pragma once
#include <vector>
#include "Entity.h"
#include "EntityManager.h"

class Physics
{
public:
    explicit Physics(float gravity = 980.0f); // pixels/sec^2, tweakable

    void setGravity(float value) { gravity = value; }
    float getGravity() const { return gravity; }

    void update(EntityManager &entities, float deltaTime);
    void update(EntityManager &entities, const std::vector<float> &timelineDeltas);
    void updateTimeline(EntityManager &entities, int timelineId, float deltaTime);

private:
    float gravity;
};
