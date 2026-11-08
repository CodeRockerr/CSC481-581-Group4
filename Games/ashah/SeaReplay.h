#pragma once
#include "SeaEvents.h"
#include <optional>

namespace Sea
{
    struct SavedObject
    {
        uint32_t id;
        bool active;
        std::optional<Transform> transform;
        std::optional<Sprite> sprite;
        std::optional<PhysicsBody> body;
        std::optional<MovementPath> path;
        std::optional<PlayerIntent> intent;
        std::optional<Pearl> pearl;
        std::optional<PowerUp> power;
        std::optional<DeathZone> deathZone;
    };

    template <class T>
    std::optional<T> saveComponent(const GameObject &object)
    {
        const auto *component = object.get<T>();
        return component ? std::optional<T>(*component) : std::nullopt;
    }

    template <class T>
    void restoreComponent(GameObject &object, const std::optional<T> &saved)
    {
        if (!saved)
            return;
        auto *component = object.get<T>();
        if (!component)
            throw std::runtime_error("Replay component is missing");
        *component = *saved;
        component->owner = &object;
    }

    inline std::vector<SavedObject> captureObjects(const GameObjectManager &objects)
    {
        std::vector<SavedObject> state;
        for (const auto &object : objects.getAll())
            state.push_back({object->getId(), object->active,
                             saveComponent<Transform>(*object), saveComponent<Sprite>(*object),
                             saveComponent<PhysicsBody>(*object), saveComponent<MovementPath>(*object),
                             saveComponent<PlayerIntent>(*object), saveComponent<Pearl>(*object),
                             saveComponent<PowerUp>(*object), saveComponent<DeathZone>(*object)});
        return state;
    }

    inline void restoreObjects(GameObjectManager &objects, const std::vector<SavedObject> &state)
    {
        for (const auto &saved : state)
        {
            auto *object = objects.find(saved.id);
            if (!object)
                throw std::runtime_error("Replay object is missing");
            object->active = saved.active;
            restoreComponent(*object, saved.transform);
            restoreComponent(*object, saved.sprite);
            restoreComponent(*object, saved.body);
            restoreComponent(*object, saved.path);
            restoreComponent(*object, saved.intent);
            restoreComponent(*object, saved.pearl);
            restoreComponent(*object, saved.power);
            restoreComponent(*object, saved.deathZone);
        }
    }

    struct ReplayState
    {
        std::vector<SavedObject> objects;
        ScaleMode scaleMode = ScaleMode::Percentage;
        Progress progress;
        PlayerMotion motion;
        EventState events;
        float sceneSeconds, messageTime, cameraX, cameraY;
        bool titleScreen, wasPaused;
        int respawnCount;
        std::string message;
        std::unordered_map<uint32_t, SDL_FPoint> previousGround, previousFish;
    };
}
