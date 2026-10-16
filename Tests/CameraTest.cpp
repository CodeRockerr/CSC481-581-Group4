#include "Camera.h"
#include "GameObjectManager.h"
#include "Components.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    Camera camera;
    camera.setViewport(200.0f, 100.0f);

    float sx = 0.0f;
    float sy = 0.0f;
    float wx = 0.0f;
    float wy = 0.0f;

    camera.worldToScreen(50.0f, 25.0f, sx, sy);
    assert(sx == 50.0f && sy == 25.0f);

    camera.setPosition(10.0f, 5.0f);
    camera.worldToScreen(50.0f, 25.0f, sx, sy);
    assert(sx == 40.0f && sy == 20.0f);
    camera.screenToWorld(sx, sy, wx, wy);
    assert(wx == 50.0f && wy == 25.0f);

    camera.setZoom(2.0f);
    camera.setPosition(0.0f, 0.0f);
    float sw = 0.0f;
    float sh = 0.0f;
    camera.worldRectToScreen(10.0f, 10.0f, 20.0f, 20.0f, sx, sy, sw, sh);
    assert(sx == 20.0f && sy == 20.0f && sw == 40.0f && sh == 40.0f);

    camera.setZoom(1.0f);
    camera.setPosition(0.0f, 0.0f);
    camera.setBounds(0.0f, 0.0f, 400.0f, 200.0f);
    camera.setPosition(500.0f, 500.0f);
    assert(camera.getX() == 200.0f && camera.getY() == 100.0f);
    camera.translate(-1000.0f, -1000.0f);
    assert(camera.getX() == 0.0f && camera.getY() == 0.0f);

    GameObjectManager objects;
    GameObject &player = objects.create("player");
    player.add<Transform>(140.0f, 40.0f, 20.0f, 20.0f);

    camera.clearBounds();
    camera.setPosition(0.0f, 0.0f);
    camera.follow(player.getId(), 20.0f, 20.0f);
    camera.update(objects);
    // View center starts at (100, 50). Player center is (150, 50) -> dx=50 -> shift by 30.
    assert(std::abs(camera.getX() - 30.0f) < 0.01f);
    assert(std::abs(camera.getY() - 0.0f) < 0.01f);

    camera.translate(50.0f, 0.0f);
    assert(std::abs(camera.getX() - 80.0f) < 0.01f);

    std::cout << "camera_test: all checks passed\n";
    return 0;
}
