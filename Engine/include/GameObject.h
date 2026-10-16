#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include "Component.h"

class GameObject
{
public:
    GameObject(uint32_t id, std::string name) : id(id), name(std::move(name)) {}
    GameObject(const GameObject &) = delete;
    GameObject &operator=(const GameObject &) = delete;

    uint32_t getId() const { return id; }
    const std::string &getName() const { return name; }
    void setName(std::string newName) { name = std::move(newName); }

    template <class T, class... Args>
    T &add(Args &&...args)
    {
        static_assert(std::is_base_of<Component, T>::value, "T must be derived from Component");
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        component->owner = this;
        T &result = *component;
        components[std::type_index(typeid(T))] = std::move(component);
        return result;
    }

    template <class T>
    T *get()
    {
        auto it = components.find(std::type_index(typeid(T)));
        return it == components.end() ? nullptr : static_cast<T *>(it->second.get());
    }

    template <class T>
    const T *get() const
    {
        auto it = components.find(std::type_index(typeid(T)));
        return it == components.end() ? nullptr : static_cast<const T *>(it->second.get());
    }

    template <class T>
    bool has() const { return components.count(std::type_index(typeid(T))) != 0; }

    template <class T>
    void remove() { components.erase(std::type_index(typeid(T))); }

    std::mutex &getMutex() const { return stateMutex; }
    bool active = true;

private:
    uint32_t id;
    std::string name;
    mutable std::mutex stateMutex;
    std::unordered_map<std::type_index, std::unique_ptr<Component>> components;
};
