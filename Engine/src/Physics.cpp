#include "Physics.h"
#include "Components.h"

#include <mutex>

Physics::Physics(float gravity) : gravity(gravity) {}

void Physics::update(EntityManager &entities, float deltaTime)
{
    for (const auto &e : entities.getEntities())
    {
        if (!e->active || !e->affectedByGravity)
        {
            continue;
        }
        e->velocityY += gravity * deltaTime;
    }
}

void Physics::update(EntityManager &entities, const std::vector<float> &timelineDeltas)
{
    for (const auto &e : entities.getEntities())
    {
        if (!e->active || !e->affectedByGravity)
        {
            continue;
        }
        if (e->timelineId < 0 || e->timelineId >= static_cast<int>(timelineDeltas.size()))
        {
            continue;
        }
        e->velocityY += gravity * timelineDeltas[e->timelineId];
    }
}

void Physics::updateTimeline(EntityManager &entities, int timelineId, float deltaTime)
{
    for (const auto &e : entities.getEntities())
    {
        if (!e->active || !e->affectedByGravity || e->timelineId != timelineId)
        {
            continue;
        }
        std::lock_guard<std::mutex> lock(e->stateMutex.get());
        e->velocityY += gravity * deltaTime;
    }
}

void Physics::update(GameObjectManager &objects, float deltaTime)
{
    objects.forEach<PhysicsBody>(
        [&](GameObject &, PhysicsBody &body)
        {
            if (!body.affectedByGravity)
            {
                return;
            }
            body.velocityY += gravity * deltaTime;
        });
}

void Physics::update(GameObjectManager &objects, const std::vector<float> &timelineDeltas)
{
    objects.forEach<PhysicsBody>(
        [&](GameObject &, PhysicsBody &body)
        {
            if (!body.affectedByGravity)
            {
                return;
            }
            if (body.timelineId < 0 || body.timelineId >= static_cast<int>(timelineDeltas.size()))
            {
                return;
            }
            body.velocityY += gravity * timelineDeltas[body.timelineId];
        });
}

void Physics::updateTimeline(GameObjectManager &objects, int timelineId, float deltaTime)
{
    objects.forEach<PhysicsBody>(
        [&](GameObject &object, PhysicsBody &body)
        {
            if (!body.affectedByGravity || body.timelineId != timelineId)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(object.getMutex());
            body.velocityY += gravity * deltaTime;
        });
}
