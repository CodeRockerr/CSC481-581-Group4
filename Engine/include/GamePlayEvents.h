#pragma once
#include "Camera.h"
#include "Event.h"
#include "EventManager.h"
#include "GameObjectManager.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

class GameplayEvents
{
public:
    // Per-game rules. Every callback is optional.
    struct Rules
    {
        // Decide what a death costs (lives, game over, cooldown...).
        // Return the spawn point name to respawn at, or nullopt for no respawn.
        // If unset, the victim respawns at the killing DeathZone's spawn point ("default" otherwise).
        // Games that suppress a death (e.g. cooldown) should usually still return a spawn point
        // if they want the old "always teleport" behavior.
        std::function<std::optional<std::string>(GameObject &victim, const Event &death)> onDeath;

        // Runs after the engine moved the subject to the spawn point and zeroed its velocity.
        std::function<void(GameObject &subject, const Event &spawn)> onSpawned;

        // Runs for every Collision event after the built-in death zone / scroll trigger handling.
        std::function<void(GameObject &player, GameObject &other, const Event &collision)> onCollision;
    };

    GameplayEvents(EventManager &events, GameObjectManager &objects, Camera &camera);
    ~GameplayEvents();
    GameplayEvents(const GameplayEvents &) = delete;
    GameplayEvents &operator=(const GameplayEvents &) = delete;

    void setRules(Rules rules) { this->rules = std::move(rules); }

    // When true, Death and Spawn events are marked shareable so Sai's network layer routes them.
    void setShareLifecycleEvents(bool share) { shareLifecycle = share; }

    // ---- Producers: call once per frame from gameUpdate (every frame, including during playback) ----

    // Raises a Collision event when the player STARTS overlapping another collider (enter only).
    // Event: source = player, target = other object, payload.normal points from other toward player,
    // payload.trigger = other's Collider::isTrigger. Also re-arms DeathZones the player has left.
    void produceCollisions(std::uint32_t playerId);

    // Raises one Death event when the player's y passes killY; re-latches once back inside.
    void produceOutOfBounds(std::uint32_t playerId, float killY, const std::string &reason = "fell out of the world");

    // ---- Helpers for game-specific producers (e.g. a hazard handler in a game) ----
    // Death: source = cause (may be null), target = victim.
    EventId raiseDeath(GameObject &victim, const GameObject *source, std::string reason);
    // Spawn: target = subject, source = the spawn point object when it exists.
    EventId raiseSpawn(GameObject &subject, const std::string &spawnPoint);

    // Spawn point a Death should use by default (zone's name, else "default").
    std::string defaultSpawnFor(const Event &death) const;

    // Replay: call from the game's restore hook (together with resetting PlayerIntent and the
    // game's own state). Clears the touching set, the out-of-bounds latch, and re-arms all DeathZones.
    void resetTransientState();

private:
    void onInput(const Event &event);
    void onCollision(const Event &event);
    void onDeath(const Event &event);
    void onSpawn(const Event &event);

    EventManager &events;
    GameObjectManager &objects;
    Camera &camera;
    Rules rules;
    std::vector<EventManager::Subscription> subscriptions;
    std::unordered_set<std::uint32_t> touching;
    bool boundsLatched = false;
    bool shareLifecycle = false;
};