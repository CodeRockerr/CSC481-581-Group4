#include "PoolAllocator.h"
#include "GameObject.h"
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    struct Counted
    {
        inline static int alive = 0;
        inline static int destroyed = 0;
        explicit Counted(int value) : value(value)
        {
            ++alive;
        }
        ~Counted()
        {
            --alive;
            ++destroyed;
        }
        int value;
    };

    struct Fallible
    {
        explicit Fallible(int value) : value(value)
        {
            if (value < 0)
            {
                throw std::invalid_argument("Negative value");
            }
        }
        int value;
    };

    struct alignas(128) Aligned
    {
        explicit Aligned(int value) : value(value) {}
        int value;
    };

    void configuration()
    {
        bool rejected = false;
        try
        {
            PoolAllocator<int> invalid(0);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        require(rejected, "Zero capacity must be rejected");

        PoolAllocator<int> pool(3);
        require(pool.capacity() == 3 && pool.size() == 0, "Incorrect initial capacity or size");
        require(pool.available() == 3 && pool.peakUsage() == 0, "Incorrect initial usage");
    }

    void exhaustion()
    {
        PoolAllocator<int> pool(2);
        auto *first = pool.create(10);
        auto *second = pool.create(20);

        require(first && second && first != second, "Objects need seperate live slots");
        require(*first == 10 && *second == 20, "Construction arguments were lost");
        require(pool.create(30) == nullptr, "A full pool must return nullptr");
        require(pool.size() == 2 && pool.available() == 0 && pool.peakUsage() == 2, "Exhaustion changed usage incorrectly");
    }

    void reuse()
    {
        PoolAllocator<int> pool(2);
        auto *first = pool.create(10);
        auto *second = pool.create(20);

        require(first && second, "Initial allocation failed");
        const auto address = reinterpret_cast<std::uintptr_t>(first);

        require(pool.destroy(first), "Live object release failed");
        require(pool.size() == 1 && pool.available() == 1, "Release did not free capacity");

        auto *replacement = pool.create(99);

        require(replacement && reinterpret_cast<std::uintptr_t>(replacement) == address, "Released slot was not reused");
        require(*replacement == 99 && *second == 20, "Reuse changed another live object");
        require(pool.peakUsage() == 2, "Incorrect peak usage after reuse");
    }

    void invalidRelease()
    {
        PoolAllocator<int> pool(1);
        PoolAllocator<int> other(1);
        int outside = 7;

        auto *object = pool.create(10);
        auto *foreign = other.create(20);

        require(object && foreign, "Initial allocation failed");
        require(!pool.destroy(nullptr), "Null pointer was accepted");
        require(!pool.destroy(&outside), "An outside object was accepted");
        require(!pool.destroy(foreign), "An object from another pool was accepted");
        require(pool.size() == 1 && other.size() == 1 && *foreign == 20, "Invalid release changed live objects");
        require(pool.destroy(object), "Valid release failed");
        require(!pool.destroy(object), "An already released slot was accepted");
        require(pool.size() == 0 && pool.available() == 1, "Repeated release corrupted capacity");
    }

    void lifetime()
    {
        require(Counted::alive == 0, "Objects leaked before lifetime test");
        Counted::destroyed = 0;

        {
            PoolAllocator<Counted> pool(2);
            require(Counted::alive == 0, "Reserving storage constructed objects");
            auto *first = pool.create(1);
            auto *second = pool.create(2);

            require(first && second && first->value == 1 && Counted::alive == 2, "Object construction failed");
            require(pool.destroy(first), "Explicit destruction failed");
            require(Counted::alive == 1 && Counted::destroyed == 1, "Explicit release did not destroy exactly one object");

            pool.clear();
            pool.clear();

            require(Counted::alive == 0 && Counted::destroyed == 2, "Clear skipped or repeated destruction");
            require(pool.size() == 0 && pool.available() == 2 && pool.peakUsage() == 2, "Clear did not preserve the expected usage values");
            require(pool.create(3) != nullptr, "Pool was unusable after clear");
        }
        require(Counted::alive == 0 && Counted::destroyed == 3, "Pool destruction did not clean up remaining objects");
    }

    void constructorFailure()
    {
        PoolAllocator<Fallible> pool(2);
        auto *existing = pool.create(7);
        require(existing != nullptr, "Initial allocation failed");
        bool rejected = false;

        try
        {
            pool.create(-1);
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        require(rejected, "Constructor exception was swallowed");
        require(pool.size() == 1 && pool.available() == 1 && pool.peakUsage() == 1, "Failed construction consumed a slot or changed usage");

        auto *next = pool.create(9);
        require(next && next->value == 9 && existing->value == 7, "Pool did not recover after constructor failure");
    }

    void alignment()
    {
        PoolAllocator<Aligned> pool(3);
        for (int i = 0; i < 3; ++i)
        {
            auto *object = pool.create(i);
            require(object && reinterpret_cast<std::uintptr_t>(object) % alignof(Aligned) == 0, "Object storage is incorrectly aligned");
            require(object->value == i, "Aligned object construction failed");
        }
    }

    void repeatedUse()
    {
        PoolAllocator<int> pool(16);
        int *objects[16]{};

        for (int round = 0; round < 100; ++round)
        {
            for (int i = 0; i < 16; ++i)
            {
                objects[i] = pool.create(round * 16 + i);
                require(objects[i] != nullptr, "Repeated allocation lost capacity");
            }
            require(pool.create(0) == nullptr, "Repeated allocation exceeded capacity");

            for (int parity = 0; parity < 2; ++parity)
            {
                for (int i = parity; i < 16; i += 2)
                {
                    require(*objects[i] == round * 16 + i, "Live object data was overwritten");
                    require(pool.destroy(objects[i]), "Repeated release failed");
                }
            }
            require(pool.size() == 0 && pool.available() == 16, "Repeated use corrupted capacity");
        }
        require(pool.peakUsage() == 16, "Incorrect peak after repeated use");
    }

    void engineAndMoveOnlyTypes()
    {
        PoolAllocator<GameObject> objects(1);
        auto *player = objects.create(42, "Player");

        require(player && player->getId() == 42 && player->getName() == "Player", "Engine GameObject construction failed");
        require(objects.destroy(player), "Engine GameObject release failed");

        PoolAllocator<std::unique_ptr<int>> pointers(1);
        auto *pointer = pointers.create(std::make_unique<int>(25));
        require(pointer && *pointer && **pointer == 25, "Move-only argument forwarding failed");
        require(pointers.destroy(pointer), "Move-only object release failed");
    }
}

static_assert(!std::is_copy_constructible_v<PoolAllocator<int>>);
static_assert(!std::is_copy_assignable_v<PoolAllocator<int>>);
static_assert(!std::is_move_constructible_v<PoolAllocator<int>>);
static_assert(!std::is_move_assignable_v<PoolAllocator<int>>);

int main()
{
    struct Test
    {
        const char *name;
        void (*run)();
    };

    const Test tests[] = {
        {"Configuration", configuration},
        {"Exhaustion", exhaustion},
        {"Reuse", reuse},
        {"Invalid Release", invalidRelease},
        {"Lifetime", lifetime},
        {"Constructor Failure", constructorFailure},
        {"Alignment", alignment},
        {"Repeated Use", repeatedUse},
        {"Engine And Move-Only Types", engineAndMoveOnlyTypes}};

    for (const auto &test : tests)
    {
        try
        {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        }
        catch (const std::exception &error)
        {
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "All 9 pool allocator checks passed\n";
}
