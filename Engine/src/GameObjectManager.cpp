#include "GameObjectManager.h"

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
