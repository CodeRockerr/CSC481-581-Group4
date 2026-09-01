#pragma once
#include "Entity.h"
#include "EntityManager.h"

// Task 3: Generic Physics System
//
// Applies a constant downward acceleration (gravity) to any entity that
// opts in via Entity::affectedByGravity. Gravity strength is configurable
// at runtime (NOT hardcoded) via setGravity(), matching the assignment's
// Physics.setGravity(float value) example.
class Physics {
public:
    explicit Physics(float gravity = 980.0f); // pixels/sec^2, tweakable

    void setGravity(float value) { gravity = value; }
    float getGravity() const { return gravity; }

    // Applies gravity to every entity in the manager that has
    // affectedByGravity == true. Called once per frame from Engine.
    void update(EntityManager& entities, float deltaTime);

private:
    float gravity;
};
