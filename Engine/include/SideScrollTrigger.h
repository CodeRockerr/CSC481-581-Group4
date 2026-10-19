
#pragma once

#include "Component.h"

class SideScrollTrigger : public Component
{
public:
    SideScrollTrigger(float moveX = 640.0f, float moveY = 0.0f)
        : offsetX(moveX), offsetY(moveY)
    {
    }

    float offsetX;
    float offsetY;

    // Prevents the same trigger from activating repeatedly.
    bool activated = false;
};
