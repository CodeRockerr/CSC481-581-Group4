#pragma once
#include "Entity.h"
// Task 5: Generic Collision Detection System
//
// Bounding-box (AABB) collision check between any two entities. This is
// generic on purpose: it takes Entity references and returns bool, so
// game code decides what to DO about a collision (that logic belongs in
// the individual game, not here).
class Collision {
public:
    static bool checkCollision(const Entity& a, const Entity& b);
};
