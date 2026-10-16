#pragma once
#include "Entity.h"
#include "GameObject.h"

class Collision
{
public:
    static bool checkCollision(const Entity &a, const Entity &b);
    static bool checkCollision(const GameObject &a, const GameObject &b);
};
