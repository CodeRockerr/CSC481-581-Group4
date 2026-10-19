#pragma once

#include "Component.h"

enum class MovementPathType
{
    Horizontal,
    Vertical,
    Circular
};

// Patterned motion for platforms and other movers.
// speed is angular rate in radians per second.
// distance is the amplitude (H/V) or radius (Circular) in world units.
class MovementPath : public Component
{
public:
    MovementPath(
        MovementPathType pathType = MovementPathType::Horizontal,
        float speed = 1.0f,
        float distance = 100.0f)
        : type(pathType), speed(speed), distance(distance)
    {
    }

    MovementPathType type;
    float speed;
    float distance;

    float centerX = 0.0f;
    float centerY = 0.0f;

    float elapsedTime = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;

    bool initialized = false;
};
