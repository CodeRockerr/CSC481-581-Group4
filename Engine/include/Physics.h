#pragma once
#include "Entity.h"
#include "EntityManager.h"

class Physics
{
public:
    explicit Physics(float gravity = 980.0f); // pixels/sec^2, tweakable

    void setGravity(float value) { gravity = value; }
    float getGravity() const { return gravity; }

    void update(EntityManager &entities, float deltaTime);

private:
    float gravity;
};
