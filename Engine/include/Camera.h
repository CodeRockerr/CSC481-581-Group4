#pragma once
#include <cstdint>

class GameObjectManager;

class Camera
{
public:
    void setPosition(float x, float y);
    void translate(float dx, float dy);
    float getX() const { return x; }
    float getY() const { return y; }

    void setViewport(float width, float height);
    float getViewportWidth() const { return viewWidth; }
    float getViewportHeight() const { return viewHeight; }

    void setZoom(float zoom);
    float getZoom() const { return zoom; }

    void setBounds(float minX, float minY, float maxX, float maxY);
    void clearBounds();
    bool hasWorldBounds() const { return hasBounds; }

    // Dead zone is half-width / half-height of the soft box around the view center.
    void follow(uint32_t objectId, float deadZoneX = 80.0f, float deadZoneY = 60.0f);
    void clearFollow();
    bool isFollowing() const { return following; }
    uint32_t getFollowId() const { return followId; }

    void update(const GameObjectManager &objects);

    void worldToScreen(float worldX, float worldY, float &screenX, float &screenY) const;
    void screenToWorld(float screenX, float screenY, float &worldX, float &worldY) const;
    void worldRectToScreen(float worldX, float worldY, float worldW, float worldH,
                           float &screenX, float &screenY, float &screenW, float &screenH) const;

private:
    void clampToBounds();

    float x = 0.0f;
    float y = 0.0f;
    float viewWidth = 1280.0f;
    float viewHeight = 720.0f;
    float zoom = 1.0f;

    bool hasBounds = false;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;

    bool following = false;
    uint32_t followId = 0;
    float deadZoneX = 80.0f;
    float deadZoneY = 60.0f;
};
