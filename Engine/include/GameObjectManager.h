#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <SDL3/SDL.h>
#include "GameObject.h"
#include "RenderItem.h"
#include "ScaleMode.h"

class GameObjectManager
{
public:
    GameObject &create(const std::string &name = "");
    GameObject &createWithId(uint32_t id, const std::string &name = "");

    void destroy(uint32_t id);
    void flushDestroyed();
    void clear();

    GameObject *find(uint32_t id);
    const GameObject *find(uint32_t id) const;
    GameObject *findByName(const std::string &name);

    std::size_t size() const { return objects.size(); }
    const std::vector<std::unique_ptr<GameObject>> &getAll() const { return objects; }

    void updateTimeline(int timelineId, float deltaTime);
    void copySnapshot(std::vector<RenderItem> &snapshot) const;
    void drawSnapshot(const std::vector<RenderItem> &snapshot, SDL_Renderer *renderer,
                      int windowWidth, int windowHeight) const;

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

    template <class... T, class Fn>
    void forEach(Fn &&fn)
    {
        for (auto &object : objects)
        {
            if (!object->active || !(true && ... && object->has<T>()))
            {
                continue;
            }
            fn(*object, *object->get<T>()...);
        }
    }

    template <class... T, class Fn>
    void forEach(Fn &&fn) const
    {
        for (const auto &object : objects)
        {
            const GameObject &constObject = *object;
            if (!constObject.active || !(true && ... && constObject.has<T>()))
            {
                continue;
            }
            fn(constObject, *constObject.get<T>()...);
        }
    }

private:
    void drawItem(const RenderItem &item, SDL_Renderer *renderer, float scaleX, float scaleY) const;

    std::vector<std::unique_ptr<GameObject>> objects;
    std::unordered_map<uint32_t, GameObject *> byId;
    std::vector<uint32_t> pendingDestroy;
    uint32_t nextId = 1;
    ScaleMode scaleMode = ScaleMode::Pixel;
    int referenceWidth = 1280;
    int referenceHeight = 800;
};
