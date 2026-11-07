#include "GamePlayEvents.h"

#include "Collision.h"
#include "Components.h"
#include "DeathZone.h"
#include "EventObjects.h"
#include "GamePlaySystems.h"
#include "InputSystem.h"
#include "NetworkIdentity.h"
#include "PlayerIntent.h"
#include "SideScrollTrigger.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <utility>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace
{
    SDL_FRect boundsOf(const GameObject &object)
    {
        const Transform *t = object.get<Transform>();
        const Collider *c = object.get<Collider>();
        return c ? c->boundsFor(*t) : t->getBounds();
    }

    // Normal of the minimum-penetration axis, pointing from `other` toward `player`.
    void contactNormal(const SDL_FRect &player, const SDL_FRect &other, float &nx, float &ny)
    {
        const float overlapX = std::min(player.x + player.w, other.x + other.w) - std::max(player.x, other.x);
        const float overlapY = std::min(player.y + player.h, other.y + other.h) - std::max(player.y, other.y);
        const float pcx = player.x + player.w * 0.5f, ocx = other.x + other.w * 0.5f;
        const float pcy = player.y + player.h * 0.5f, ocy = other.y + other.h * 0.5f;
        if (overlapX < overlapY)
        {
            nx = pcx < ocx ? -1.0f : 1.0f;
            ny = 0.0f;
        }
        else
        {
            nx = 0.0f;
            ny = pcy < ocy ? -1.0f : 1.0f;
        }
    }
}

GameplayEvents::GameplayEvents(EventManager &events, GameObjectManager &objects, Camera &camera)
    : events(events), objects(objects), camera(camera)
{
    subscriptions.push_back(events.subscribe(EventTypes::Input, [this](const Event &e)
                                             { onInput(e); }));
    subscriptions.push_back(events.subscribe(EventTypes::Collision, [this](const Event &e)
                                             { onCollision(e); }));
    subscriptions.push_back(events.subscribe(EventTypes::Death, [this](const Event &e)
                                             { onDeath(e); }));
    subscriptions.push_back(events.subscribe(EventTypes::Spawn, [this](const Event &e)
                                             { onSpawn(e); }));
}

GameplayEvents::~GameplayEvents()
{
    for (auto subscription : subscriptions)
        events.unsubscribe(subscription);
}

// ---------------------------------------------------------------- producers

void GameplayEvents::produceCollisions(std::uint32_t playerId)
{
    GameObject *player = objects.find(playerId);
    if (!player || !player->active || !player->has<Transform>() || !player->has<Collider>())
    {
        touching.clear();
        return;
    }

    std::unordered_set<std::uint32_t> nowTouching;
    objects.forEach<Transform, Collider>(
        [&](GameObject &other, Transform &, Collider &collider)
        {
            if (&other == player || !Collision::checkCollision(*player, other))
                return;
            nowTouching.insert(other.getId());
            if (touching.count(other.getId()))
                return; // enter events only

            CollisionEvent payload;
            payload.trigger = collider.isTrigger;
            contactNormal(boundsOf(*player), boundsOf(other), payload.normalX, payload.normalY);

            Event event;
            event.type = EventTypes::Collision;
            event.source = EventObjects::of(*player);
            event.target = EventObjects::of(other);
            event.payload = payload;
            events.raise(std::move(event));
        });

    // A death zone fires once per entry: re-arm when the player is no longer inside it.
    objects.forEach<DeathZone>(
        [&](GameObject &zone, DeathZone &deathZone)
        {
            if (!nowTouching.count(zone.getId()))
                deathZone.armed = true;
        });

    touching = std::move(nowTouching);
}

void GameplayEvents::produceOutOfBounds(std::uint32_t playerId, float killY, const std::string &reason)
{
    GameObject *player = objects.find(playerId);
    Transform *transform = player && player->active ? player->get<Transform>() : nullptr;
    if (!transform)
    {
        boundsLatched = false;
        return;
    }
    if (transform->y > killY)
    {
        if (!boundsLatched)
        {
            boundsLatched = true;
            raiseDeath(*player, nullptr, reason);
        }
    }
    else
        boundsLatched = false;
}

EventId GameplayEvents::raiseDeath(GameObject &victim, const GameObject *source, std::string reason)
{
    Event event;
    event.type = EventTypes::Death;
    event.priority = EventPriority::High;
    if (source)
        event.source = EventObjects::of(*source);
    event.target = EventObjects::of(victim);
    event.payload = DeathEvent{std::move(reason)};
    event.shareable = shareLifecycle;
    return events.raise(std::move(event));
}

EventId GameplayEvents::raiseSpawn(GameObject &subject, const std::string &spawnPoint)
{
    Event event;
    event.type = EventTypes::Spawn;
    event.priority = EventPriority::High;
    if (GameObject *point = GameplaySystems::findSpawnPointByName(objects, spawnPoint))
    {
        const auto source = EventObjects::of(*point);
        if (!shareLifecycle || !EventObjects::isOffline(source))
            event.source = source;
    }
    event.target = EventObjects::of(subject);
    event.payload = SpawnEvent{spawnPoint};
    event.shareable = shareLifecycle;
    return events.raise(std::move(event));
}

std::string GameplayEvents::defaultSpawnFor(const Event &death) const
{
    if (GameObject *source = EventObjects::resolve(objects, death.source))
        if (const DeathZone *zone = source->get<DeathZone>())
            return zone->spawnPointName;
    return "default";
}

// ---------------------------------------------------------------- replay state

GameplayEvents::TransientState GameplayEvents::captureTransientState() const
{
    TransientState state;
    state.touching = touching;
    state.boundsLatched = boundsLatched;
    // getAll() rather than forEach(): inactive zones keep their armed flag too.
    for (const auto &object : objects.getAll())
        if (const DeathZone *zone = object->get<DeathZone>())
            state.deathZoneArmed.emplace_back(object->getId(), zone->armed);
    return state;
}

void GameplayEvents::restoreTransientState(const TransientState &state)
{
    touching = state.touching;
    boundsLatched = state.boundsLatched;

    // Zones missing from the snapshot (created after capture) start armed.
    for (const auto &object : objects.getAll())
        if (DeathZone *zone = object->get<DeathZone>())
            zone->armed = true;

    for (const auto &entry : state.deathZoneArmed)
        if (GameObject *object = objects.find(entry.first))
            if (DeathZone *zone = object->get<DeathZone>())
                zone->armed = entry.second;
}

void GameplayEvents::resetTransientState()
{
    touching.clear();
    boundsLatched = false;
    for (const auto &object : objects.getAll())
        if (DeathZone *zone = object->get<DeathZone>())
            zone->armed = true;
}

// ---------------------------------------------------------------- handlers

void GameplayEvents::onInput(const Event &event)
{
    const auto *input = std::get_if<InputEvent>(&event.payload);
    GameObject *object = EventObjects::resolve(objects, event.target);
    if (!input || !object)
        return;
    PlayerIntent *intent = object->get<PlayerIntent>();
    if (!intent)
        return;

    const char *action = input->action.c_str();
    if (std::strcmp(action, InputActions::MoveX) == 0)
        intent->moveX = input->value;
    else if (std::strcmp(action, InputActions::JumpHeld) == 0)
        intent->jumpHeld = input->value != 0.0f;
    else if (std::strcmp(action, InputActions::JumpPressed) == 0)
        intent->jumpPressed = input->value != 0.0f;
    else if (std::strcmp(action, InputActions::DownHeld) == 0)
        intent->downHeld = input->value != 0.0f;
}

void GameplayEvents::onCollision(const Event &event)
{
    if (!std::get_if<CollisionEvent>(&event.payload))
        return;
    GameObject *player = EventObjects::resolve(objects, event.source);
    GameObject *other = EventObjects::resolve(objects, event.target);
    if (!player || !other)
        return;

    if (DeathZone *zone = other->get<DeathZone>(); zone && zone->armed)
    {
        zone->armed = false;
        raiseDeath(*player, other, "deathzone");
    }

    if (SideScrollTrigger *trigger = other->get<SideScrollTrigger>(); trigger && !trigger->activated)
    {
        camera.translate(trigger->offsetX, trigger->offsetY);
        trigger->activated = true;
    }

    if (rules.onCollision)
        rules.onCollision(*player, *other, event);
}

void GameplayEvents::onDeath(const Event &event)
{
    GameObject *victim = EventObjects::resolve(objects, event.target);
    if (!victim)
        return;
    if (const auto *identity = victim->get<NetworkIdentity>(); identity && identity->isReplica)
        return;
    const std::optional<std::string> spawn =
        rules.onDeath ? rules.onDeath(*victim, event) : std::optional<std::string>(defaultSpawnFor(event));
    if (spawn)
        raiseSpawn(*victim, *spawn); // child event: runs in the same drain
}

void GameplayEvents::onSpawn(const Event &event)
{
    const auto *spawn = std::get_if<SpawnEvent>(&event.payload);
    GameObject *subject = EventObjects::resolve(objects, event.target);
    if (!spawn || !subject)
        return;

    GameObject *point = GameplaySystems::findSpawnPointByName(objects, spawn->spawnPoint);
    if (!point)
        point = GameplaySystems::findSpawnPointByIndex(objects, 0); // fallback: first spawn point
    if (!point)
        return;

    bool moved = false;
    {
        std::lock_guard<std::mutex> lock(subject->getMutex());
        Transform *transform = subject->get<Transform>();
        const Transform *target = point->get<Transform>();
        if (transform && target)
        {
            transform->x = target->x;
            transform->y = target->y;
            if (PhysicsBody *body = subject->get<PhysicsBody>())
            {
                body->velocityX = 0.0f;
                body->velocityY = 0.0f;
            }
            moved = true;
        }
    }
    if (moved && rules.onSpawned)
        rules.onSpawned(*subject, event);
}
