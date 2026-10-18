#include "Camera.h"
#include "GameObjectManager.h"
#include "Components.h"

#include <algorithm>
#include <mutex>

void Camera::setPosition(float newX, float newY)
{
    x = newX;
    y = newY;
    clampToBounds();
}

void Camera::translate(float dx, float dy)
{
    x += dx;
    y += dy;
    clampToBounds();
}

void Camera::setViewport(float width, float height)
{
    viewWidth = width;
    viewHeight = height;
    clampToBounds();
}

void Camera::setZoom(float newZoom)
{
    zoom = newZoom > 0.01f ? newZoom : 0.01f;
}

void Camera::setBounds(float boundMinX, float boundMinY, float boundMaxX, float boundMaxY)
{
    hasBounds = true;
    minX = boundMinX;
    minY = boundMinY;
    maxX = boundMaxX;
    maxY = boundMaxY;
    clampToBounds();
}

void Camera::clearBounds()
{
    hasBounds = false;
}

void Camera::follow(uint32_t objectId, float deadX, float deadY)
{
    following = true;
    followId = objectId;
    deadZoneX = deadX;
    deadZoneY = deadY;
}

void Camera::clearFollow()
{
    following = false;
    followId = 0;
}

void Camera::update(const GameObjectManager &objects)
{
    if (!following)
    {
        return;
    }

    const GameObject *target = objects.find(followId);
    if (!target || !target->active)
    {
        return;
    }

    const Transform *transform = target->get<Transform>();
    if (!transform)
    {
        return;
    }

    float centerX = 0.0f;
    float centerY = 0.0f;
    {
        std::lock_guard<std::mutex> lock(target->getMutex());
        centerX = transform->x + transform->width * 0.5f;
        centerY = transform->y + transform->height * 0.5f;
    }

    const float viewCenterX = x + viewWidth * 0.5f;
    const float viewCenterY = y + viewHeight * 0.5f;
    const float dx = centerX - viewCenterX;
    const float dy = centerY - viewCenterY;

    if (dx > deadZoneX)
    {
        x += dx - deadZoneX;
    }
    else if (dx < -deadZoneX)
    {
        x += dx + deadZoneX;
    }

    if (dy > deadZoneY)
    {
        y += dy - deadZoneY;
    }
    else if (dy < -deadZoneY)
    {
        y += dy + deadZoneY;
    }

    clampToBounds();
}

void Camera::worldToScreen(float worldX, float worldY, float &screenX, float &screenY) const
{
    screenX = (worldX - x) * zoom;
    screenY = (worldY - y) * zoom;
}

void Camera::screenToWorld(float screenX, float screenY, float &worldX, float &worldY) const
{
    worldX = screenX / zoom + x;
    worldY = screenY / zoom + y;
}

void Camera::worldRectToScreen(float worldX, float worldY, float worldW, float worldH,
                               float &screenX, float &screenY, float &screenW, float &screenH) const
{
    worldToScreen(worldX, worldY, screenX, screenY);
    screenW = worldW * zoom;
    screenH = worldH * zoom;
}

void Camera::clampToBounds()
{
    if (!hasBounds)
    {
        return;
    }

    const float maxCamX = maxX - viewWidth;
    const float maxCamY = maxY - viewHeight;
    x = std::clamp(x, minX, std::max(minX, maxCamX));
    y = std::clamp(y, minY, std::max(minY, maxCamY));
}
