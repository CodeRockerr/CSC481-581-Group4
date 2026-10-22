#include "SeaGameplay.h"
#include "DeathZone.h"
#include "GamePlaySystems.h"
#include "SpawnPoint.h"
#include "Timeline.h"
#include <cstdlib>
#include <iostream>

namespace
{
    void require(bool ok, const char *message)
    {
        if (!ok)
        {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }
    GameObject &makePlayer(GameObjectManager &objects, float x, float y)
    {
        auto &o = objects.create("captain");
        o.add<Transform>(x, y, Sea::DiverWidth, Sea::DiverHeight);
        o.add<Collider>(.25f, .25f, .08f, 0);
        o.add<PhysicsBody>(-1, true);
        return o;
    }
    GameObject &makeGround(GameObjectManager &objects, float x, float y, float w)
    {
        auto &o = objects.create("platform");
        o.add<Transform>(x, y, w, 40);
        o.add<Collider>();
        o.add<Sea::Ground>();
        return o;
    }
}
int main()
{
    Physics physics;
    physics.setGravity(Sea::Gravity);
    {
        // The game must actually use the shared configurable Physics instance.
        GameObjectManager objects;
        auto &player = makePlayer(objects, 200, 100);
        auto &body = *player.get<PhysicsBody>();
        Sea::PlayerMotion motion;
        physics.setGravity(300);
        Sea::movePlayer(objects, player, motion, physics, .1f, 0, false, {});
        const float slowFall = body.velocityY;
        player.get<Transform>()->y = 100;
        body.velocityY = 0;
        motion = {};
        physics.setGravity(600);
        Sea::movePlayer(objects, player, motion, physics, .1f, 0, false, {});
        require(std::fabs(body.velocityY - slowFall * 2) < .01f,
                "changing the engine gravity must change the diver's acceleration");
        physics.setGravity(Sea::Gravity);
    }
    {
        Timeline realTime;
        Timeline gameTime(&realTime, 1'000'000, .5);
        Timeline childTime(&gameTime, 100'000, 2);
        gameTime.step();
        childTime.step();
        SDL_Delay(30);
        const double half = gameTime.step(), normal = childTime.step();
        require(half > .005 && normal > half * 1.8 && normal < half * 2.2,
                "arbitrarily anchored timelines must compose their scales");
        gameTime.pause();
        const int64_t stopped = childTime.getTime();
        SDL_Delay(20);
        require(childTime.getTime() == stopped, "pausing an anchor must freeze its child");
        gameTime.unpause();
        gameTime.setScale(2);
        gameTime.step();
        SDL_Delay(20);
        require(gameTime.step() > .025, "unpausing at double speed must resume elapsed time");
        childTime.setTicSize(200'000);
        require(childTime.getTicSize() == 200'000, "timeline tic size must remain adjustable");
    }
    for (float dt : {1.f / 120, 1.f / 60, 1.f / 30, .1f})
    {
        GameObjectManager objects;
        auto &ground = makeGround(objects, 0, 460, 1280);
        auto &player = makePlayer(objects, 200, 150);
        Sea::PlayerMotion m;
        for (int i = 0; i < 4 / dt; ++i)
            Sea::movePlayer(objects, player, m, physics, dt, 0, false, {});
        require(m.grounded && std::fabs(player.get<Transform>()->y + Sea::DiverHeight - 460) < .01f,
                "visible feet must land on platforms at different frame rates");
        float highestFeet = 460;
        for (int i = 0; i < 3 / dt; ++i)
        {
            Sea::movePlayer(objects, player, m, physics, dt, 0, i == 0, {});
            highestFeet = std::min(highestFeet, player.get<Transform>()->y + Sea::DiverHeight);
        }
        require(highestFeet >= 395 && highestFeet <= 420 && m.grounded,
                "the dry jump must be a controlled 40-65px hop and land again at every frame rate");
        const float oldX = player.get<Transform>()->x, oldY = player.get<Transform>()->y;
        Sea::movePlayer(objects, player, m, physics, 0, 1, true, {}, true, true, true);
        require(player.get<Transform>()->x == oldX && player.get<Transform>()->y == oldY,
                "pause must freeze the player even while powered");
        Sea::movePlayer(objects, player, m, physics, .05f, 0, false, {}, false, true);
        require(!m.grounded && player.get<Transform>()->y > oldY, "S must drop through a shelf");
        ground.get<Transform>()->y = 2000;
        player.get<Transform>()->y = 900;
        m = {};
        for (int i = 0; i < 4 / dt; ++i)
            Sea::movePlayer(objects, player, m, physics, dt, 0, false, {}, true);
        require(player.get<Transform>()->y < Sea::SurfaceY, "upward swimming must recover missed pearls at any depth");
    }
    {
        GameObjectManager objects;
        auto &ground = makeGround(objects, 0, 460, 1280);
        auto &player = makePlayer(objects, 200, 460 - Sea::DiverHeight);
        Sea::PlayerMotion m;
        Sea::movePlayer(objects, player, m, physics, 1.f / 60, 0, false, {});
        ground.get<Transform>()->x += 25;
        ground.get<Transform>()->y -= 20;
        Sea::movePlayer(objects, player, m, physics, 1.f / 60, 0, false, {{ground.getId(), {25, -20}}});
        require(m.grounded && std::fabs(player.get<Transform>()->x - 225) < .01f &&
                    std::fabs(player.get<Transform>()->y + Sea::DiverHeight - 440) < .01f,
                "moving shelves carry their riders");
        player.get<Transform>()->y = 1000;
        player.get<Transform>()->x = 300;
        m = {};
        for (int i = 0; i < 60; ++i)
            Sea::movePlayer(objects, player, m, physics, 1.f / 60, 1, false, {});
        const float normalVelocity = player.get<PhysicsBody>()->velocityX;
        for (int i = 0; i < 60; ++i)
            Sea::movePlayer(objects, player, m, physics, 1.f / 60, 1, false, {}, false, false, true);
        require(player.get<PhysicsBody>()->velocityX > normalVelocity * 1.3f, "overdrive must increase swimming speed");
        Sea::movePlayer(objects, player, m, physics, 1.f / 120, 0, false, {});
        require(player.get<PhysicsBody>()->velocityX > 0, "water drag must slow drift gradually, not stop instantly");
    }
    {
        for (int art = 0; art < 9; ++art)
        {
            GameObjectManager objects;
            const bool dock = art == 8;
            const auto kind = static_cast<Sea::PlatformArt>(art % 8);
            const float pad = dock ? .31f : Sea::topPadding(kind);
            auto &ground = objects.create();
            ground.add<Transform>(300, 1200 - 120 * pad, 400, 120);
            ground.add<Collider>(Sea::PlatformSidePad, Sea::PlatformSidePad, pad, 0);
            ground.add<Sea::Ground>();
            if (dock)
                ground.add<Sea::Shore>();
            auto &player = makePlayer(objects, 430, 1200 - Sea::DiverHeight);
            auto &t = *player.get<Transform>();
            auto &body = *player.get<PhysicsBody>();
            body.velocityY = -100;
            Sea::PlayerMotion motion;
            for (int i = 0; i < 60; ++i)
                Sea::movePlayer(objects, player, motion, physics, 1.f / 60, .2f, false, {}, true);
            require(motion.grounded && std::fabs(t.y + t.height - 1200) < .01f &&
                        Sea::diverFrame(t, motion, .3f, true, false) < 4,
                    "all nine platform types must latch touching feet and walk despite a held swim key");
            Sea::movePlayer(objects, player, motion, physics, .05f, 0, true, {}, true);
            require(!motion.grounded && t.y + t.height < 1200,
                    "a fresh jump press must release a latched diver");
            if (!dock)
            {
                t.x = 430;
                t.y = 1208 - Sea::DiverHeight;
                body.velocityY = -Sea::SwimSpeed;
                motion = {};
                for (int i = 0; i < 30; ++i)
                    Sea::movePlayer(objects, player, motion, physics, 1.f / 60, 0, false, {}, true);
                require(motion.grounded && !Sea::isSwimming(t, motion),
                        "rising feet touching a platform top must settle into the walking state");
            }
        }
    }
    {
        Sea::Progress p;
        require(Sea::PowerDuration < 4, "overdrive must last less than four seconds");
        p.activatePower();
        p.tick(.5f, .1f);
        require(std::fabs(p.powerTime - 2.9f) < .001f, "power duration must not depend on the selected player speed");
        p.activatePower();
        for (int i = 0; i < 40; ++i)
        {
            require(!p.hit(), "fish and urchin contacts must not consume lives during overdrive");
            p.tick(.05f);
        }
        require(p.health == Sea::MaxHealth && p.powered(), "shield must survive repeated contacts");
        p.tick(1.1f);
        require(!p.powered() && p.hit() && p.health == Sea::MaxHealth - 1, "power must expire and damage resume");
        require(!p.hit(), "respawn grace must prevent stacked damage");
        p.tick(2);
        require(p.hit(), "damage must resume after respawn grace");
    }
    {
        Sea::Progress p;
        require(p.hit() && p.health == 2, "damage must spend exactly one life");
        p.tick(60);
        p.checkpoint = 2;
        p.reachedBottom = true;
        p.pearls.fill(true);
        Sea::updateMission(p, {600, Sea::RecoveryDepth, 48, 74});
        p.checkpoint = 0;
        require(p.health == 2, "time, checkpoints and returning to shore must not restore lives");
        require(p.hit() && p.health == 1, "the second death must leave one life");
        p.tick(2);
        require(p.hit() && p.health == 0 && p.gameOver, "the third death must end the run without an automatic refill");
        p.tick(60);
        require(!p.hit() && p.health == 0, "an ended run must stay at zero until an explicit restart");
        p = {};
        require(p.health == Sea::MaxHealth && !p.finished(), "a new run must start with three lives");
    }
    {
        GameObjectManager objects;
        const int index = 6;
        const auto &d = Sea::Pearls[index];
        require(d.movingIndex == 5, "the added pearl must belong to the circular shelf");
        const auto &m = Sea::Movers[d.movingIndex];
        auto &shelf = objects.create();
        float x, y;
        Sea::moverStart(d.movingIndex, x, y);
        shelf.add<Transform>(x, y, m.width, m.height);
        shelf.add<Sea::Ground>(d.movingIndex);
        shelf.add<MovementPath>(m.path, m.speed, m.amplitude);
        auto &pearl = objects.create();
        pearl.add<Transform>(d.x, d.y, d.size, d.size);
        pearl.add<Sea::Pearl>();
        for (int i = 0; i < 120; ++i)
        {
            GameplaySystems::updateMovementPaths(objects, .1f);
            Sea::updatePearl(objects, pearl, index, i * .1f);
            const auto &platform = *shelf.get<Transform>();
            const auto &pt = *pearl.get<Transform>();
            const float deck = platform.y + platform.height * Sea::topPadding(m.art);
            require(std::fabs(pt.x + pt.width * .5f - platform.x - platform.width * .5f) < .001f &&
                        deck - pt.y - pt.height >= .9f && deck - pt.y - pt.height <= 11.1f,
                    "the pearl must stay centered and above the moving shelf throughout its orbit");
        }
        Sea::Progress p;
        p.pearls.fill(true);
        p.pearls[index] = false;
        Sea::updateMission(p, {600, Sea::RecoveryDepth, 48, 74});
        require(!p.returning, "the rotating-shelf pearl must be required to unlock the return mission");
        p.pearls[index] = true;
        require(Sea::updateMission(p, {600, Sea::RecoveryDepth, 48, 74}), "collecting all nine pearls must unlock the return");
    }
    {
        GameObjectManager objects;
        for (const auto &d : Sea::StaticPlatforms)
            makeGround(objects, d.x + d.width * Sea::PlatformSidePad, d.solidTopY,
                       d.width * (1 - 2 * Sea::PlatformSidePad));
        for (const auto &s : Sea::Spawns)
        {
            auto &player = makePlayer(objects, s.x, s.y);
            Sea::PlayerMotion m;
            Sea::movePlayer(objects, player, m, physics, 1.f / 60, 0, false, {});
            require(m.grounded, "every checkpoint must be safe on a real platform");
        }
        for (int i = 0; i < Sea::PearlCount; ++i)
        {
            const auto &p = Sea::Pearls[i];
            require(p.x > 28 && p.x + p.size < Sea::WorldWidth - 28 && p.y > Sea::SurfaceY && p.y < Sea::WorldHeight - 100,
                    "every pearl must be in the swimmable bounds above the exit");
            for (const auto &urchin : Sea::Urchins)
            {
                require(!Sea::overlaps({p.x, p.y, p.size, p.size}, {urchin.x(), urchin.y(), urchin.width, Sea::UrchinHeight}),
                        "pearls must not be placed inside urchin clusters");
                require(!Sea::overlaps({p.x + p.size * .5f - 24, p.y, 48, 220},
                                       {urchin.x(), urchin.y(), urchin.width, Sea::UrchinHeight}),
                        "a diver must have a clear upward approach to every pearl");
            }
        }
    }
    {
        GameObjectManager objects;
        makeGround(objects, 100, 920, 340);
        auto &player = makePlayer(objects, 230, 920 - Sea::DiverHeight);
        auto &t = *player.get<Transform>();
        Sea::PlayerMotion motion;
        for (int i = 0; i < 15; ++i)
            Sea::movePlayer(objects, player, motion, physics, 1.f / 60, 1, false, {});
        require(motion.grounded && Sea::diverFrame(t, motion, .2f, true, false) < 4,
                "walking underwater must use diver.png rather than the swimming sheet");
        const auto walk = Sea::diverDrawBounds(t);
        for (int i = 0; i < 90 && motion.grounded; ++i)
            Sea::movePlayer(objects, player, motion, physics, 1.f / 60, 1, false, {});
        require(Sea::isSwimming(t, motion) && Sea::diverFrame(t, motion, .2f, true, false) >= 8,
                "walking off an underwater ledge must immediately select the swimming pose");
        const auto swim = Sea::diverDrawBounds(t);
        require(walk.w == swim.w && walk.h == swim.h && std::fabs(swim.y + swim.h - t.y - t.height) < .001f,
                "changing pose must preserve drawing size and the feet anchor");
        require(t.width == Sea::DiverWidth && t.height == Sea::DiverHeight,
                "drawing a larger swimmer must not resize the collision body");
        require(Sea::diverFrame(t, motion, .2f, true, true) >= 12,
                "powered swimmers must carry both swimming state and yellow-suit frames");
        for (const auto &d : Sea::Urchins)
        {
            const auto &platform = Sea::StaticPlatforms[d.platform];
            require(d.x() >= platform.x && d.x() + d.width <= platform.x + platform.width &&
                        d.y() + Sea::UrchinHeight == platform.solidTopY + 4,
                    "urchin clusters must be attached to their actual platform surface");
        }
        auto &urchin = objects.create();
        urchin.add<Transform>(500, 900, 75, Sea::UrchinHeight);
        urchin.add<Sea::Urchin>();
        t.x = 480;
        t.y = 870;
        require(Sea::touchesHazard(player, urchin), "urchin contact must hit its visible round silhouette");
        t.x = 380;
        require(!Sea::touchesHazard(player, urchin), "empty water beside an urchin must be safe");
    }
    {
        GameObjectManager objects;
        auto &player = makePlayer(objects, 972, Sea::SurfaceY + 100);
        Sea::PlayerMotion m;
        Sea::Progress p;
        p.pearls.fill(true);
        require(!p.returning, "pearls alone cannot skip visiting the bottom");
        Sea::updateMission(p, {600, Sea::RecoveryDepth, 48, 74});
        require(p.returning && p.reachedBottom, "bottom recovery must start the return-to-surface mission");
        bool crossed = false;
        for (int i = 0; i < 240; ++i)
        {
            const auto before = Sea::bounds(player);
            Sea::movePlayer(objects, player, m, physics, 1.f / 60, 0, false, {}, true);
            crossed |= Sea::crossesExit(before, Sea::bounds(player), m, p);
        }
        require(crossed, "a swimming stroke must breach the surface and pass through the buoy opening");
        SDL_FRect before{990, Sea::GateY - 60, 48, 62}, after{990, Sea::GateY - 64, 48, 62};
        p.pearls[0] = false;
        require(!Sea::crossesExit(before, after, m, p), "one missing pearl must lock the exit");
        p.pearls.fill(true);
        require(!Sea::crossesExit(after, before, m, p), "descending through the buoy must not win");
        p.reachedBottom = false;
        require(!Sea::crossesExit(before, after, m, p), "the bottom recovery must be completed before exiting");
        p.reachedBottom = true;
        after.x = Sea::GateRight;
        require(!Sea::crossesExit(before, after, m, p), "swimming outside the buoy hole must not win");
        auto &shore = makeGround(objects, 60, 460, 440);
        shore.add<Sea::Shore>();
        player.get<Transform>()->x = 220;
        player.get<Transform>()->y = 460 - Sea::DiverHeight;
        player.get<PhysicsBody>()->velocityY = 0;
        m = {};
        for (int i = 0; i < 60; ++i)
            Sea::movePlayer(objects, player, m, physics, 1.f / 60, 0, false, {}, false, true);
        require(m.grounded, "S must not let the captain phase through dry land");
        const float time = p.elapsed;
        p.won = true;
        p.tick(1);
        require(p.elapsed == time, "mission time must stop when the captain wins");
    }
    {
        GameObjectManager objects;
        auto &pearl = objects.create();
        pearl.add<Sea::Pearl>().collected = true;
        pearl.add<Sprite>().visible = false;
        pearl.active = false;
        auto &power = objects.create();
        power.add<Sea::PowerUp>().collected = true;
        power.add<Sprite>();
        power.active = false;
        Sea::restoreCollectibles(objects);
        require(pearl.active && !pearl.get<Sea::Pearl>()->collected && pearl.get<Sprite>()->visible &&
                    power.active && !power.get<Sea::PowerUp>()->collected,
                "restart must restore pearls and power pickups");
        auto &spawn = objects.create();
        spawn.add<Transform>(560, 300, 24, 24);
        spawn.add<SpawnPoint>("departure");
        auto &pit = objects.create();
        pit.add<Transform>(0, 4140, 1280, 10000);
        pit.add<Collider>(0, 0, 0, 0, true);
        pit.add<DeathZone>("departure");
        auto &player = makePlayer(objects, 400, 4150);
        player.get<PhysicsBody>()->velocityY = 300;
        GameplaySystems::updateDeathZones(objects, player.getId());
        require(player.get<Transform>()->x == 560 && player.get<Transform>()->y == 300 &&
                    player.get<PhysicsBody>()->velocityY == 0,
                "the abyss boundary must respawn and clear velocity");
        require(!spawn.has<Sprite>() && !pit.has<Sprite>(), "spawn and death objects must stay hidden");
    }
    std::cout << "PASS: frame rates, landing, jump/drop/swim, riders, boost, timed immunity, checkpoints, pearls, exit gate, restart, respawn.\n";
}
