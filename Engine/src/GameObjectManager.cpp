#include "GameObjectManager.h"
#include "Components.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <string>

GameObject &GameObjectManager::create(const std::string &name)
{
    return createWithId(nextId++, name);
}

GameObject &GameObjectManager::createWithId(uint32_t id, const std::string &name)
{
    if (id == 0)
    {
        throw std::invalid_argument("GameObject id 0 is reserved");
    }

    if (byId.count(id) != 0)
    {
        throw std::invalid_argument("GameObject id " + std::to_string(id) + " already exists");
    }

    if (id >= nextId)
    {
        nextId = id + 1;
    }

    auto object = std::make_unique<GameObject>(id, name);
    GameObject &result = *object;
    byId[id] = &result;
    objects.push_back(std::move(object));
    return result;
}

void GameObjectManager::destroy(uint32_t id)
{
    if (byId.count(id) == 0)
    {
        return;
    }

    for (uint32_t pending : pendingDestroy)
    {
        if (pending == id)
        {
            return;
        }
    }

    pendingDestroy.push_back(id);
}

void GameObjectManager::flushDestroyed()
{
    for (uint32_t id : pendingDestroy)
    {
        auto mapIt = byId.find(id);
        if (mapIt == byId.end())
        {
            continue;
        }

        byId.erase(mapIt);

        for (auto it = objects.begin(); it != objects.end(); ++it)
        {
            if ((*it)->getId() == id)
            {
                objects.erase(it);
                break;
            }
        }
    }

    pendingDestroy.clear();
}

void GameObjectManager::clear()
{
    pendingDestroy.clear();
    byId.clear();
    objects.clear();
    nextId = 1;
}

GameObject *GameObjectManager::find(uint32_t id)
{
    auto it = byId.find(id);
    return it == byId.end() ? nullptr : it->second;
}

const GameObject *GameObjectManager::find(uint32_t id) const
{
    auto it = byId.find(id);
    return it == byId.end() ? nullptr : it->second;
}

GameObject *GameObjectManager::findByName(const std::string &name)
{
    for (auto &object : objects)
    {
        if (object->getName() == name)
        {
            return object.get();
        }
    }

    return nullptr;
}

void GameObjectManager::updateTimeline(int timelineId, float deltaTime)
{
    forEach<Transform, PhysicsBody>(
        [&](GameObject &object, Transform &transform, PhysicsBody &body)
        {
            if (body.timelineId != timelineId)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(object.getMutex());
            transform.x += body.velocityX * deltaTime;
            transform.y += body.velocityY * deltaTime;
        });
}

void GameObjectManager::copySnapshot(std::vector<RenderItem> &snapshot) const
{
    snapshot.clear();

    forEach<Transform, Sprite>(
        [&](const GameObject &object, const Transform &transform, const Sprite &sprite)
        {
            std::lock_guard<std::mutex> lock(object.getMutex());

            RenderItem item;
            item.x = transform.x;
            item.y = transform.y;
            item.width = transform.width;
            item.height = transform.height;
            item.color = sprite.color;
            item.texture = sprite.texture;
            item.frame = sprite.frame;
            item.frameCount = sprite.frameCount;
            item.flipHorizontal = sprite.flipHorizontal;
            item.visible = sprite.visible;
            item.layer = sprite.layer;
            item.active = object.active;
            snapshot.push_back(item);
        });

    std::stable_sort(snapshot.begin(), snapshot.end(),
                     [](const RenderItem &a, const RenderItem &b)
                     {
                         return a.layer < b.layer;
                     });
}

void GameObjectManager::drawSnapshot(const std::vector<RenderItem> &snapshot, SDL_Renderer *renderer,
                                     int windowWidth, int windowHeight, const Camera &camera) const
{
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (scaleMode == ScaleMode::Percentage)
    {
        scaleX = static_cast<float>(windowWidth) / static_cast<float>(referenceWidth);
        scaleY = static_cast<float>(windowHeight) / static_cast<float>(referenceHeight);
    }

    for (const RenderItem &item : snapshot)
    {
        drawItem(item, renderer, scaleX, scaleY, camera);
    }
}

void GameObjectManager::drawItem(const RenderItem &item, SDL_Renderer *renderer, float scaleX, float scaleY,
                                 const Camera &camera) const
{
    if (!item.active || !item.visible)
    {
        return;
    }

    float screenX = 0.0f;
    float screenY = 0.0f;
    float screenW = 0.0f;
    float screenH = 0.0f;
    camera.worldRectToScreen(item.x, item.y, item.width, item.height, screenX, screenY, screenW, screenH);

    SDL_FRect rect{
        screenX * scaleX,
        screenY * scaleY,
        screenW * scaleX,
        screenH * scaleY};

    if (item.texture)
    {
        float textureWidth = 0.0f;
        float textureHeight = 0.0f;
        SDL_GetTextureSize(item.texture, &textureWidth, &textureHeight);

        float frameWidth = textureWidth / static_cast<float>(item.frameCount);
        SDL_FRect source{
            frameWidth * static_cast<float>(item.frame),
            0.0f,
            frameWidth,
            textureHeight};

        const SDL_FlipMode flip = item.flipHorizontal ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_SetTextureColorMod(item.texture, item.color.r, item.color.g, item.color.b);
        SDL_SetTextureAlphaMod(item.texture, item.color.a);
        SDL_RenderTextureRotated(renderer, item.texture, &source, &rect, 0.0, nullptr, flip);
        SDL_SetTextureColorMod(item.texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(item.texture, 255);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, item.color.r, item.color.g, item.color.b, item.color.a);
        SDL_RenderFillRect(renderer, &rect);
    }
}
