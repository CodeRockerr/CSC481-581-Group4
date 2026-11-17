#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

template <class T>
class PoolAllocator
{
    static_assert(std::is_object_v<T> && !std::is_array_v<T>, "T must be a non-array object type");
    static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>, "T must not be const or volatile");
    static_assert(std::is_nothrow_destructible_v<T>, "T must have a non-throwing destructor");

public:
    explicit PoolAllocator(std::size_t capacity)
        : capacity_(checkedCapacity(capacity)),
          freeSlots_(capacity_), live_(capacity_, false)
    {
        for (std::size_t i = 0; i < capacity_; ++i)
        {
            freeSlots_[i] = capacity_ - i - 1;
        }
        storage_ = std::allocator_traits<std::allocator<T>>::allocate(allocator_, capacity_);
    }

    ~PoolAllocator()
    {
        clear();
        std::allocator_traits<std::allocator<T>>::deallocate(allocator_, storage_, capacity_);
    }

    PoolAllocator(const PoolAllocator &) = delete;
    PoolAllocator &operator=(const PoolAllocator &) = delete;
    PoolAllocator(PoolAllocator &&) = delete;
    PoolAllocator &operator=(PoolAllocator &&) = delete;

    template <class... Args>
    T *create(Args &&...args)
    {
        if (freeSlots_.empty())
        {
            return nullptr;
        }

        const std::size_t index = freeSlots_.back();
        freeSlots_.pop_back();

        T *object;
        try
        {
            object = ::new (static_cast<void *>(storage_ + index))
                T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            freeSlots_.push_back(index);
            throw;
        }
        live_[index] = true;
        ++size_;
        if (size_ > peakUsage_)
        {
            peakUsage_ = size_;
        }
        return object;
    }

    bool destroy(T *object) noexcept
    {
        if (!object)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(storage_);
        const auto address = reinterpret_cast<std::uintptr_t>(object);
        if (address < base)
        {
            return false;
        }

        const auto offset = address - base;
        const auto index = offset / sizeof(T);
        if (offset % sizeof(T) != 0 || index >= capacity_ || !live_[index])
        {
            return false;
        }

        std::destroy_at(storage_ + index);
        live_[index] = false;
        --size_;
        freeSlots_.push_back(index);
        return true;
    }

    void clear() noexcept
    {
        for (std::size_t i = 0; i < capacity_; ++i)
        {
            if (live_[i])
            {
                std::destroy_at(storage_ + i);
                live_[i] = false;
            }
        }
        freeSlots_.clear();
        for (std::size_t i = 0; i < capacity_; ++i)
        {
            freeSlots_.push_back(capacity_ - i - 1);
        }
        size_ = 0;
    }

    std::size_t size() const noexcept
    {
        return size_;
    }
    std::size_t capacity() const noexcept
    {
        return capacity_;
    }
    std::size_t peakUsage() const noexcept
    {
        return peakUsage_;
    }
    std::size_t available() const noexcept
    {
        return freeSlots_.size();
    }

private:
    static std::size_t checkedCapacity(std::size_t capacity)
    {
        if (capacity == 0)
        {
            throw std::invalid_argument("Pool capacity must be greater than zero");
        }
        return capacity;
    }

    std::size_t capacity_;
    std::allocator<T> allocator_;
    T *storage_ = nullptr;
    std::vector<std::size_t> freeSlots_;
    std::vector<bool> live_;
    std::size_t size_ = 0;
    std::size_t peakUsage_ = 0;
};
