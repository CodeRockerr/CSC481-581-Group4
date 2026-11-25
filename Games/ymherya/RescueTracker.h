#pragma once
// Bookkeeping for Hello Kitty's networked rescue. No engine or SDL dependencies, so it can be unit tested.
//
// Every fall gets a unique id. The fallen player remembers ONLY her currently open fall and when it expires,
// and accepts the first Rescue event that carries that id. Rescuers keep a list of falls they can still rescue
// and drop an entry when its window runs out or when anyone else's Rescue for that same fall arrives.

#include <cstdint>
#include <map>
#include <optional>
#include <set>

namespace Kitty
{
    // Fallen player's side: at most one open fall at a time.
    class FallTracker
    {
    public:
        explicit FallTracker(uint32_t owner) : owner_(owner) {}

        // Start a new fall (replaces any open one). Returns its id, unique across clients.
        int64_t beginFall(double windowSeconds)
        {
            id_ = (static_cast<int64_t>(owner_) << 32) | static_cast<int64_t>(++counter_);
            left_ = windowSeconds;
            return id_;
        }

        void tick(double seconds)
        {
            if (id_ == 0)
                return;
            left_ -= seconds;
            if (left_ <= 0.0)
                id_ = 0; // window expired: late rescues are ignored
        }

        // True only for the first rescue that matches the open fall before it expires.
        bool tryAccept(int64_t fallId)
        {
            if (id_ == 0 || fallId != id_ || left_ <= 0.0)
                return false;
            id_ = 0; // closed: a second rescuer for the same fall is rejected
            return true;
        }

        bool isOpen() const { return id_ != 0; }

    private:
        uint32_t owner_;
        uint32_t counter_ = 0;
        int64_t id_ = 0;
        double left_ = 0.0;
    };

    // Rescuer's side: falls (one per fallen player) that can still be rescued.
    class PendingRescues
    {
    public:
        struct Entry
        {
            uint32_t owner;
            int64_t fallId;
        };

        void add(uint32_t owner, int64_t fallId, double windowSeconds)
        {
            pending_[owner] = {fallId, windowSeconds}; // a newer fall replaces an older one
        }

        void tick(double seconds)
        {
            for (auto it = pending_.begin(); it != pending_.end();)
            {
                it->second.left -= seconds;
                it = it->second.left <= 0.0 ? pending_.erase(it) : std::next(it);
            }
        }

        // Pick the first waiting fall for a local R press and remove it from the list.
        std::optional<Entry> takeFirst()
        {
            if (pending_.empty())
                return std::nullopt;
            const auto it = pending_.begin();
            const Entry entry{it->first, it->second.fallId};
            pending_.erase(it);
            return entry;
        }

        // Someone else already rescued this fall. A stale id never clears a newer fall.
        bool clear(uint32_t owner, int64_t fallId)
        {
            const auto it = pending_.find(owner);
            if (it == pending_.end() || it->second.fallId != fallId)
                return false;
            pending_.erase(it);
            return true;
        }

        bool empty() const { return pending_.empty(); }
        bool has(uint32_t owner) const { return pending_.count(owner) != 0; }
        double firstTimeLeft() const { return pending_.empty() ? 0.0 : pending_.begin()->second.left; }

    private:
        struct Slot
        {
            int64_t fallId;
            double left;
        };
        std::map<uint32_t, Slot> pending_;
    };

    // Rescuer's memory of the rescues it SENT. Sending is not winning: the fallen player decides, and tells
    // everyone with a RescueAccepted event naming the winner. Only then does a rescuer say it succeeded.
    class RescueAttempts
    {
    public:
        enum class Result
        {
            NotMine, // this fall is not one we tried to rescue
            Won,     // our rescue was the one accepted
            TooLate  // someone else's rescue was accepted first
        };

        void sent(int64_t fallId) { sent_.insert(fallId); }

        // Called when a RescueAccepted event for `fallId` arrives; `winner` is the accepted rescuer's client id.
        Result resolve(int64_t fallId, uint32_t winner, uint32_t me)
        {
            if (sent_.erase(fallId) == 0)
                return Result::NotMine;
            return winner == me ? Result::Won : Result::TooLate;
        }

    private:
        std::set<int64_t> sent_;
    };
}