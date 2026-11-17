// Milestone 5: GameObjectManager running on PoolAllocator<GameObject>.
// Uses require() instead of assert() so the checks also run in Release builds.
#include "Components.h"
#include "GameObjectManager.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    // Counts live instances so the tests can see component destructors run.
    struct Tracked : Component
    {
        inline static int alive = 0;
        Tracked() { ++alive; }
        ~Tracked() override { --alive; }
    };

    template <class Fn>
    bool throwsBadAlloc(Fn &&fn)
    {
        try
        {
            fn();
        }
        catch (const std::bad_alloc &)
        {
            return true;
        }
        return false;
    }

    // Existing games never call usePool(); nothing may change for them.
    void heapModeIsUnchanged()
    {
        GameObjectManager objects;
        require(!objects.usesPool(), "A new manager must start on the heap");
        require(objects.poolCapacity() == 0 && objects.poolInUse() == 0, "Heap mode reports no pool");

        for (int i = 0; i < 500; ++i)
        {
            require(objects.tryCreate("heap") != nullptr, "Heap mode has no capacity limit");
        }
        require(objects.size() == 500, "Heap objects must be tracked");
        require(objects.find(1) != nullptr && objects.find(500) != nullptr, "Heap ids stay sequential");

        objects.destroy(1);
        objects.flushDestroyed();
        require(objects.find(1) == nullptr && objects.size() == 499, "Heap destroy still works");
    }

    void poolMustBeSelectedWhileEmpty()
    {
        GameObjectManager objects;
        objects.create("early");

        bool rejected = false;
        try
        {
            objects.usePool(8);
        }
        catch (const std::logic_error &)
        {
            rejected = true;
        }
        require(rejected, "usePool must refuse a manager that already owns objects");
        require(!objects.usesPool(), "A refused usePool must leave the manager on the heap");

        objects.clear();
        objects.usePool(8);
        require(objects.usesPool() && objects.poolCapacity() == 8, "usePool works after clear()");

        bool zeroRejected = false;
        try
        {
            GameObjectManager other;
            other.usePool(0);
        }
        catch (const std::invalid_argument &)
        {
            zeroRejected = true;
        }
        require(zeroRejected, "A zero-capacity pool must be rejected");
    }

    void exhaustionChangesNothing()
    {
        GameObjectManager objects;
        objects.usePool(3);

        objects.create("a");
        objects.create("b");
        GameObject &last = objects.create("c");
        require(last.getId() == 3, "Pooled ids are sequential");
        require(objects.poolInUse() == 3 && objects.poolAvailable() == 0, "Pool must be full");

        require(objects.tryCreate("d") == nullptr, "tryCreate returns nullptr when full");
        require(throwsBadAlloc([&]
                               { objects.create("d"); }),
                "create throws bad_alloc when full");
        require(throwsBadAlloc([&]
                               { objects.createWithId(40, "d"); }),
                "createWithId throws bad_alloc when full");

        require(objects.size() == 3 && objects.poolInUse() == 3, "A failed create must not add an object");
        require(objects.find(4) == nullptr && objects.find(40) == nullptr, "A failed create must not register an id");

        objects.destroy(1);
        objects.flushDestroyed();
        require(objects.create("e").getId() == 4, "A failed create must not use up an id");
    }

    void destroyIsDeferredAndSlotsAreReused()
    {
        GameObjectManager objects;
        objects.usePool(2);

        GameObject &first = objects.create("first");
        first.add<Tracked>();
        objects.create("second").add<Tracked>();
        const GameObject *firstAddress = &first;
        const std::uint32_t firstId = first.getId();

        objects.destroy(firstId);
        objects.destroy(firstId); // a second request for the same id is ignored
        require(objects.find(firstId) == &first, "A destroyed object stays valid until the flush");
        require(objects.poolInUse() == 2, "The slot is not released before the flush");
        require(objects.tryCreate("too-early") == nullptr, "A pending destroy does not free a slot yet");

        objects.flushDestroyed();
        require(objects.find(firstId) == nullptr, "The object is gone after the flush");
        require(Tracked::alive == 1, "Destroying a pooled object must destroy its components");
        require(objects.poolInUse() == 1 && objects.poolAvailable() == 1, "The flush releases the slot");

        GameObject &reused = objects.create("reused");
        require(&reused == firstAddress, "A released slot is reused");
        require(reused.getId() != firstId, "A reused slot gets a new id, so old ids cannot alias it");
        require(!reused.has<Tracked>() && reused.active, "A reused slot holds a freshly constructed object");
        require(reused.getName() == "reused", "A reused slot carries the new name");

        objects.clear();
        require(Tracked::alive == 0, "clear() must destroy every component");
    }

    // Restarting a level: everything goes back to the pool and the block is kept.
    void clearReturnsEverythingAndKeepsThePool()
    {
        GameObjectManager objects;
        objects.usePool(16);

        std::vector<const GameObject *> firstRun;
        for (int i = 0; i < 16; ++i)
        {
            GameObject &object = objects.create("run-one");
            object.add<Tracked>();
            firstRun.push_back(&object);
        }
        objects.destroy(5); // a destroy still queued at restart must not break clear()

        objects.clear();
        require(objects.size() == 0 && objects.poolInUse() == 0, "clear() returns every object");
        require(objects.poolAvailable() == 16 && objects.poolCapacity() == 16, "clear() keeps the reserved block");
        require(objects.poolPeakUsage() == 16, "Peak usage survives a restart");
        require(Tracked::alive == 0, "clear() destroys components");
        objects.flushDestroyed(); // stale request from before the clear: must be a no-op

        for (int round = 0; round < 3; ++round)
        {
            for (int i = 0; i < 16; ++i)
            {
                GameObject &object = objects.create("again");
                bool sameBlock = false;
                for (const GameObject *address : firstRun)
                {
                    sameBlock = sameBlock || address == &object;
                }
                require(sameBlock, "A restarted level reuses the same storage");
            }
            require(objects.find(1) != nullptr && objects.find(16) != nullptr, "Ids restart at 1 after clear()");
            require(objects.tryCreate("overflow") == nullptr, "Capacity is unchanged after a restart");
            objects.clear();
        }
    }

    void duplicateIdDoesNotLeakASlot()
    {
        GameObjectManager objects;
        objects.usePool(4);
        objects.createWithId(7, "remote");

        bool rejected = false;
        try
        {
            objects.createWithId(7, "duplicate");
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        require(rejected, "Duplicate ids are still rejected");
        require(objects.poolInUse() == 1, "A rejected id must not take a slot");
        require(objects.create("local").getId() == 8, "Local ids continue after an explicit id");
    }

    // Pooled objects must behave like any other object for the rest of the engine.
    void systemsSeePooledObjects()
    {
        GameObjectManager objects;
        objects.usePool(8);

        GameObject &mover = objects.create("mover");
        mover.add<Transform>(0.0f, 0.0f, 10.0f, 10.0f);
        mover.add<PhysicsBody>(0, false).velocityX = 100.0f;
        mover.add<Sprite>(SDL_Color{255, 0, 0, 255});
        objects.create("still").add<Transform>(5.0f, 5.0f, 1.0f, 1.0f);

        int visited = 0;
        objects.forEach<Transform>([&](GameObject &, Transform &)
                                   { ++visited; });
        require(visited == 2, "forEach visits pooled objects");
        require(objects.findByName("still") != nullptr, "findByName sees pooled objects");
        require(objects.getAll().size() == 2, "getAll lists pooled objects");

        objects.updateTimeline(0, 0.5f);
        require(mover.get<Transform>()->x == 50.0f, "Movement integrates pooled objects");

        std::vector<RenderItem> snapshot;
        objects.copySnapshot(snapshot);
        require(snapshot.size() == 1, "Only pooled objects with a Sprite are drawn");
    }

    // A bullet-style workload: many short-lived objects through a small pool.
    void churnStaysWithinCapacity()
    {
        GameObjectManager objects;
        objects.usePool(32);

        std::vector<std::uint32_t> live;
        int dropped = 0;
        for (int frame = 0; frame < 2000; ++frame)
        {
            for (int shot = 0; shot < 3; ++shot)
            {
                if (GameObject *bullet = objects.tryCreate("bullet"))
                {
                    bullet->add<Tracked>();
                    live.push_back(bullet->getId());
                }
                else
                {
                    ++dropped;
                }
            }
            if (frame % 2 == 0)
            {
                for (std::size_t i = 0; i < live.size() && i < 5; ++i)
                {
                    objects.destroy(live[i]);
                }
                live.erase(live.begin(), live.begin() + (live.size() < 5 ? live.size() : 5));
            }
            objects.flushDestroyed();
            require(objects.poolInUse() <= 32, "Usage must never pass capacity");
            require(objects.size() == objects.poolInUse(), "Manager and pool must agree on the live count");
            require(static_cast<std::size_t>(Tracked::alive) == objects.size(), "No component may leak");
        }
        require(dropped > 0, "The workload must actually reach the capacity limit");
        require(objects.poolPeakUsage() == 32, "Peak usage reaches capacity");
    }

    // The manager destroys its objects before its pool. With live objects left, this
    // must not crash or leak (run under AddressSanitizer to confirm).
    void managerDestructionReleasesLiveObjects()
    {
        {
            GameObjectManager objects;
            objects.usePool(4);
            objects.create("left-over").add<Tracked>();
            objects.create("left-over").add<Tracked>();
            require(Tracked::alive == 2, "Setup");
        }
        require(Tracked::alive == 0, "Destroying the manager destroys its pooled objects");
    }
}

int main()
{
    struct Check
    {
        const char *name;
        void (*run)();
    };
    const Check checks[] = {
        {"heap mode is unchanged", heapModeIsUnchanged},
        {"pool must be selected while empty", poolMustBeSelectedWhileEmpty},
        {"exhaustion changes nothing", exhaustionChangesNothing},
        {"destroy is deferred and slots are reused", destroyIsDeferredAndSlotsAreReused},
        {"clear returns everything and keeps the pool", clearReturnsEverythingAndKeepsThePool},
        {"duplicate id does not leak a slot", duplicateIdDoesNotLeakASlot},
        {"systems see pooled objects", systemsSeePooledObjects},
        {"churn stays within capacity", churnStaysWithinCapacity},
        {"manager destruction releases live objects", managerDestructionReleasesLiveObjects},
    };

    int passed = 0;
    for (const Check &check : checks)
    {
        try
        {
            check.run();
            ++passed;
        }
        catch (const std::exception &error)
        {
            std::cerr << "FAILED: " << check.name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "All " << passed << " pooled object manager checks passed\n";
    return 0;
}
