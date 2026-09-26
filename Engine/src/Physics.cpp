#include "Physics.h"

Physics::Physics(float gravity) : gravity(gravity) {}

void Physics::update(EntityManager& entities, float deltaTime) {
    for (const auto& e : entities.getEntities()) {
        if (!e->active || !e->affectedByGravity) continue;
        e->velocityY += gravity * deltaTime;
    }
}

void Physics::update(EntityManager& entities, const std::vector<float>& timelineDeltas) {
    for (const auto& e : entities.getEntities()) {
        if (!e->active || !e->affectedByGravity) continue;
        if (e->timelineId < 0 || e->timelineId >= static_cast<int>(timelineDeltas.size())) continue;
        e->velocityY += gravity * timelineDeltas[e->timelineId];
    }
}
