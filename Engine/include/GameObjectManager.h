#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>
#include <SDL3/SDL.h>
#include "GameObject.h"
#include "PoolAllocator.h"
#include "RenderItem.h"
#include "ScaleMode.h"
#include "Camera.h"

// Returns a GameObject to wherever it came from: its pool slot, or the heap.
// Stored inside every owning pointer, so the manager never has to guess.
struct GameObjectDeleter
{
    PoolAllocator<GameObject> *pool = nullptr;

    void operator()(GameObject *object) const noexcept
    {
        if (pool)
            pool->destroy(object);
        else
            delete object;
    }
};

using GameObjectPtr = std::unique_ptr<GameObject, GameObjectDeleter>;

class GameObjectManager
{
public:
    GameObjectManager() = default;

    // Not copyable or movable. With pooled storage a defaulted move assignment would be
    // unsafe: members are assigned in declaration order, so `pool` would be replaced (and the
    // old block freed) before this manager's old objects are released, and their deleters
    // would return memory to a pool that no longer exists. Engine owns exactly one manager
    // and nothing moves it, so moving is disabled rather than hand-written.
    GameObjectManager(const GameObjectManager &) = delete;
    GameObjectManager &operator=(const GameObjectManager &) = delete;
    GameObjectManager(GameObjectManager &&) = delete;
    GameObjectManager &operator=(GameObjectManager &&) = delete;

    // ---- Pooled storage (Milestone 5) ----
    // Opt in: reserve storage for `capacity` GameObjects up front. Every object created
    // afterwards lives in that block instead of the heap. Call it while the manager is
    // empty (before the first create(), or right after clear()); it throws std::logic_error
    // otherwise, because live objects cannot be moved between storages.
    // Games that never call it keep the old heap behavior.
    void usePool(std::size_t capacity);
    bool usesPool() const { return pool != nullptr; }
    std::size_t poolCapacity() const { return pool ? pool->capacity() : 0; }
    std::size_t poolInUse() const { return pool ? pool->size() : 0; }
    std::size_t poolAvailable() const { return pool ? pool->available() : 0; }
    std::size_t poolPeakUsage() const { return pool ? pool->peakUsage() : 0; }

    // With a pool, create()/createWithId() throw std::bad_alloc when the pool is full.
    // A failed create changes nothing: no id is used up and no slot is taken.
    GameObject &create(const std::string &name = "");
    GameObject &createWithId(uint32_t id, const std::string &name = "");
    // Same as create(), but returns nullptr when the pool is full. Use it for objects the
    // game can live without (bullets, particles). Never returns nullptr without a pool.
    GameObject *tryCreate(const std::string &name = "");

    void destroy(uint32_t id);
    void flushDestroyed();
    void clear();

    GameObject *find(uint32_t id);
    const GameObject *find(uint32_t id) const;
    GameObject *findByName(const std::string &name);

    std::size_t size() const { return objects.size(); }
    const std::vector<GameObjectPtr> &getAll() const { return objects; }

    void updateTimeline(int timelineId, float deltaTime);
    void copySnapshot(std::vector<RenderItem> &snapshot) const;
    void drawSnapshot(const std::vector<RenderItem> &snapshot, SDL_Renderer *renderer,
                      int windowWidth, int windowHeight, const Camera &camera) const;

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
    void drawItem(const RenderItem &item, SDL_Renderer *renderer, float scaleX, float scaleY,
                  const Camera &camera) const;

    // Declared before `objects` on purpose: members are destroyed in reverse order, so
    // every object goes back to the pool before the pool releases its storage.
    std::unique_ptr<PoolAllocator<GameObject>> pool;
    std::vector<GameObjectPtr> objects;
    std::unordered_map<uint32_t, GameObject *> byId;
    std::vector<uint32_t> pendingDestroy;
    uint32_t nextId = 1;
    ScaleMode scaleMode = ScaleMode::Pixel;
    int referenceWidth = 1280;
    int referenceHeight = 800;
};
