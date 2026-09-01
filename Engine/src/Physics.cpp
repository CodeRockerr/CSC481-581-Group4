#include "Physics.h"

Physics::Physics(float gravity) : gravity(gravity) {}

void Physics::update(EntityManager& entities, float deltaTime) {
    for (const auto& e : entities.getEntities()) {
        if (!e->active || !e->affectedByGravity) continue;
        e->velocityY += gravity * deltaTime;
    }
}
