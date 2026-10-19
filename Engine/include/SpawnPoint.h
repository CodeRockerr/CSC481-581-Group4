
#pragma once

#include "Component.h"

#include <string>
#include <utility>

class SpawnPoint : public Component
{
public:
    explicit SpawnPoint(std::string pointName = "default")
        : name(std::move(pointName))
    {
    }

    std::string name;
};
