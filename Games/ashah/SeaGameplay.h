#pragma once
#include "SeaLevel.h"
#include "Components.h"
#include "GameObjectManager.h"
#include "Physics.h"
#include "Collision.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace Sea
{
    inline SDL_FRect bounds(const GameObject &object)
    {
        const Transform &t = *object.get<Transform>();
        const Collider *c = object.get<Collider>();
        return c ? c->boundsFor(t) : t.getBounds();
    }
    inline bool overlaps(const SDL_FRect &a, const SDL_FRect &b)
    {
        return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
    }
    struct Progress
    {
        int health = MaxHealth, checkpoint = 0;
        std::array<bool, PearlCount> pearls{};
        float powerTime = 0, graceTime = 0, elapsed = 0;
        bool reachedBottom = false, returning = false, won = false, gameOver = false;
        bool finished() const { return won || gameOver; }
        int count() const { return static_cast<int>(std::count(pearls.begin(), pearls.end(), true)); }
        bool powered() const { return powerTime > 0; }
        bool protectedNow() const { return powered() || graceTime > 0; }
        void tick(float dt, float powerDelta = -1)
        {
            if (finished())
                return;
            elapsed += dt;
            powerTime = std::max(0.0f, powerTime - (powerDelta < 0 ? dt : powerDelta));
            graceTime = std::max(0.0f, graceTime - dt);
        }
        void activatePower() { powerTime = PowerDuration; }
        bool loseLife()
        {
            if (finished())
                return false;
            health = std::max(0, health - 1);
            gameOver = health == 0;
            powerTime = 0;
            graceTime = 1.8f;
            return true;
        }
        bool hit() { return !protectedNow() && loseLife(); }
    };
    inline void updatePearl(GameObjectManager &objects, GameObject &pearl, int index, float seconds)
    {
        const auto &d = Pearls[index];
        auto &t = *pearl.get<Transform>();
        t.x = d.x;
        t.y = d.y;
        if (d.movingIndex >= 0)
            objects.forEach<Transform, Ground>([&](GameObject &, Transform &platform, Ground &ground)
                                               {
                if (ground.movingIndex != d.movingIndex) return;
                t.x = platform.x + (platform.width - d.size) * .5f;
                t.y = platform.y + platform.height * topPadding(Movers[d.movingIndex].art) - d.size - 6; });
        t.y += std::sin(seconds * 2.5f + index) * 5;
    }
    struct PlayerMotion
    {
        bool grounded = false, wasSubmerged = false, enteredWater = false;
        uint32_t ridingId = 0;
        float coyoteTime = 0, jumpBuffer = 0, dropTime = 0, jumpDetachTime = 0;
    };
    inline bool isSwimming(const Transform &t, const PlayerMotion &motion)
    {
        return !motion.grounded && t.y + t.height > SurfaceY;
    }
    inline int diverFrame(const Transform &t, const PlayerMotion &motion,
                          float elapsed, bool moving, bool powered)
    {
        const bool swimming = isSwimming(t, motion);
        const int beat = static_cast<int>(elapsed * (powered ? 13.f : 8.f));
        return (motion.grounded && !moving ? 0 : swimming ? beat % 4
                                                          : 1 + beat % 3) +
               (powered ? 4 : 0) + (swimming ? 8 : 0);
    }
    inline SDL_FRect diverDrawBounds(const Transform &t)
    {
        return {t.x + (t.width - DiverDrawWidth) * .5f,
                t.y + t.height - DiverDrawHeight, DiverDrawWidth, DiverDrawHeight};
    }
    inline void movePlayer(GameObjectManager &objects, GameObject &player,
                           PlayerMotion &motion, Physics &physics, float delta, float axis, bool jump,
                           const std::unordered_map<uint32_t, SDL_FPoint> &groundDelta,
                           bool swim = false, bool dive = false, bool powered = false)
    {
        if (delta <= 0)
            return;
        Transform &t = *player.get<Transform>();
        PhysicsBody &body = *player.get<PhysicsBody>();
        if (motion.grounded && objects.find(motion.ridingId))
        {
            auto found = groundDelta.find(motion.ridingId);
            if (found != groundDelta.end())
            {
                t.x += found->second.x;
                t.y += found->second.y;
            }
        }
        if (jump)
            motion.jumpBuffer = 0.14f;
        motion.enteredWater = false;
        const int steps = std::max(1, static_cast<int>(std::ceil(delta * 120)));
        const float dt = delta / steps;
        for (int i = 0; i < steps; ++i)
        {
            motion.coyoteTime = motion.grounded ? 0.10f : std::max(0.0f, motion.coyoteTime - dt);
            GameObject *standingOn = objects.find(motion.ridingId);
            if (dive && motion.grounded && !(standingOn && (standingOn->has<RecoveryBed>() || standingOn->has<Shore>())))
            {
                motion.dropTime = 0.28f;
                motion.grounded = false;
                motion.ridingId = 0;
                motion.coyoteTime = 0;
                t.y += 3;
            }
            if (motion.jumpBuffer > 0 && motion.coyoteTime > 0)
            {
                const float wet = std::clamp((t.y + t.height - SurfaceY) / t.height, 0.f, 1.f);
                body.velocityY = wet < .2f ? -JumpSpeed : -SwimSpeed;
                motion.grounded = false;
                motion.ridingId = 0;
                motion.jumpDetachTime = .18f;
                motion.coyoteTime = motion.jumpBuffer = 0;
            }
            motion.jumpBuffer = std::max(0.0f, motion.jumpBuffer - dt);
            motion.dropTime = std::max(0.0f, motion.dropTime - dt);
            motion.jumpDetachTime = std::max(0.0f, motion.jumpDetachTime - dt);
            const SDL_FRect before = bounds(player);
            const float wet = std::clamp((t.y + t.height - SurfaceY) / t.height, 0.f, 1.f);
            const int region = t.y < RegionStarts[1] ? 0 : t.y < RegionStarts[2] ? 1
                                                                                 : 2;
            const float current = wet > .8f && !motion.grounded
                                      ? std::sin(t.y * .005f) * (8 + region * 10.f)
                                      : 0;
            const float targetX = axis * (powered ? BoostSpeed : MoveSpeed) + current;

            const float response = wet > .1f && !motion.grounded ? 4.8f : 18.f;
            body.velocityX += (targetX - body.velocityX) * std::min(1.f, response * dt);
            if (wet > .1f && !motion.grounded)
            {
                motion.enteredWater |= !motion.wasSubmerged && wet > .8f;
                if (wet > .8f)
                    motion.wasSubmerged = true;
                const float targetY = swim && !dive ? -(powered ? SwimSpeed * 1.3f : SwimSpeed)
                                      : dive        ? MaxFallSpeed * 1.4f
                                                    : 38.f;
                body.velocityY += (targetY - body.velocityY) * 3.5f * dt * wet;
                physics.updateTimeline(objects, body.timelineId, dt);

                body.velocityY -= physics.getGravity() * (1 - std::pow(1 - wet, 3)) * dt;

                if (motion.wasSubmerged && wet < .75f && swim && !dive && body.velocityY < 0)
                {
                    body.velocityY = -BreachSpeed;
                    motion.wasSubmerged = false;
                }
            }
            else
                physics.updateTimeline(objects, body.timelineId, dt);
            body.velocityY = std::clamp(body.velocityY, -std::max(JumpSpeed, BreachSpeed), 700.f);
            objects.updateTimeline(body.timelineId, dt);
            t.x = std::clamp(t.x, 28.f, WorldWidth - t.width - 28);
            t.y = std::max(40.f, t.y);
            if (t.y <= 40 && body.velocityY < 0)
                body.velocityY = 0;
            const SDL_FRect after = bounds(player);
            motion.grounded = false;
            motion.ridingId = 0;
            float landingY = WorldHeight + 10000;
            objects.forEach<Ground>([&](GameObject &ground, Ground &)
                                    {
                    if (motion.dropTime > 0 && !ground.has<RecoveryBed>() && !ground.has<Shore>()) return;
                    const SDL_FRect surface = bounds(ground);
                    const auto movement = groundDelta.find(ground.getId());
                    const float rise = i == 0 && movement != groundDelta.end()
                        ? std::max(0.0f, -movement->second.y) : 0;
                    const float oldFeet = before.y + before.h, newFeet = after.y + after.h;
                    const bool falling = body.velocityY >= 0 && oldFeet <= surface.y + 1.5f + rise && newFeet >= surface.y;
                    // A held swim stroke must not pull touching feet off a shelf.
                    // Rising feet can settle on its top too; a fresh jump detaches.
                    const bool touching = motion.jumpDetachTime <= 0 &&
                        (std::fabs(oldFeet - surface.y) <= 1.5f ||
                         (wet > .1f && !ground.has<Shore>() && oldFeet >= surface.y && newFeet <= surface.y));
                    if ((falling || touching) && after.x < surface.x + surface.w &&
                        after.x + after.w > surface.x && surface.y < landingY)
                    { landingY = surface.y; motion.ridingId = ground.getId(); } });
            if (motion.ridingId)
            {
                t.y += landingY - (after.y + after.h);
                body.velocityY = 0;
                motion.grounded = true;
                motion.jumpBuffer = 0;
            }
        }
    }
    inline bool touchesHazard(const GameObject &player, const GameObject &hazard)
    {
        const auto p = bounds(player);
        if (!hazard.has<Urchin>())
            return Collision::checkCollision(player, hazard);
        const auto s = hazard.get<Transform>()->getBounds();
        if (!overlaps(p, s))
            return false;
        const int count = urchinCount(s.w);
        const float step = s.w / count, radius = urchinRadius(s.w);
        for (int i = 0; i < count; ++i)
        {
            const float cx = s.x + (i + .5f) * step, cy = s.y + 21;
            const float dx = cx - std::clamp(cx, p.x, p.x + p.w);
            const float dy = cy - std::clamp(cy, p.y, p.y + p.h);
            if (dx * dx + dy * dy < radius * radius)
                return true;
        }
        return false;
    }
    inline bool updateMission(Progress &progress, const SDL_FRect &player)
    {
        if (progress.finished())
            return false;
        progress.reachedBottom |= player.y + player.h >= RecoveryDepth;
        const bool ready = progress.reachedBottom && progress.count() == PearlCount;
        const bool changed = ready && !progress.returning;
        progress.returning |= ready;
        return changed;
    }
    inline bool crossesExit(const SDL_FRect &before, const SDL_FRect &after,
                            const PlayerMotion &, const Progress &progress)
    {
        return !progress.finished() && progress.returning && progress.reachedBottom && progress.count() == PearlCount &&
               before.y + before.h >= GateY && after.y + after.h < GateY &&
               after.x >= GateLeft && after.x + after.w <= GateRight;
    }
    inline void restoreCollectibles(GameObjectManager &objects)
    {
        for (const auto &o : objects.getAll())
        {
            bool collectible = false;
            if (auto *p = o->get<Pearl>())
            {
                p->collected = false;
                collectible = true;
            }
            if (auto *p = o->get<PowerUp>())
            {
                p->collected = false;
                collectible = true;
            }
            if (collectible)
            {
                o->active = true;
                o->get<Sprite>()->visible = true;
            }
        }
    }
}
