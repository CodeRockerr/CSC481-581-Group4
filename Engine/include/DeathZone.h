
#pragma once

#include "Component.h"

#include <string>
#include <utility>

class DeathZone : public Component
{
public:
    explicit DeathZone(std::string spawnName = "default")
        : spawnPointName(std::move(spawnName))
    {
    }

    std::string spawnPointName;
};
