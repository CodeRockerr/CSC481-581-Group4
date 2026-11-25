// Regression test for the rescue review comment (lives next to the game, Games/ymherya/): each fall has an id, the fallen player accepts only the
// first matching rescue before the window expires, and other rescuers drop that fall from their lists.
#include "RescueTracker.h"

#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(cond)                                                      \
    do                                                                   \
    {                                                                    \
        if (!(cond))                                                     \
        {                                                                \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failures;                                                  \
        }                                                                \
    } while (0)

int main()
{
    using namespace Kitty;

    // Two rescuers (clients 2 and 3) both rescue client 1's fall: only the first counts.
    {
        FallTracker fallen(1);
        PendingRescues rescuer2, rescuer3;
        const int64_t fall = fallen.beginFall(7.0);
        rescuer2.add(1, fall, 6.0);
        rescuer3.add(1, fall, 6.0);

        const auto first = rescuer2.takeFirst();
        const auto second = rescuer3.takeFirst(); // both pressed R at the same moment
        CHECK(first && second);
        CHECK(fallen.tryAccept(first->fallId));   // first rescue accepted
        CHECK(!fallen.tryAccept(second->fallId)); // second rescue for the same fall rejected
    }

    // A third rescuer who only SEES the first rescue drops that fall and cannot rescue it again.
    {
        FallTracker fallen(1);
        PendingRescues rescuer3;
        const int64_t fall = fallen.beginFall(7.0);
        rescuer3.add(1, fall, 6.0);
        CHECK(rescuer3.clear(1, fall));
        CHECK(rescuer3.empty());
        CHECK(!rescuer3.takeFirst());
    }

    // A rescue that arrives after the window expired is ignored.
    {
        FallTracker fallen(1);
        const int64_t fall = fallen.beginFall(7.0);
        fallen.tick(7.5);
        CHECK(!fallen.isOpen());
        CHECK(!fallen.tryAccept(fall));
    }

    // A rescue for an OLD fall cannot rescue a newer one (she fell again).
    {
        FallTracker fallen(1);
        const int64_t oldFall = fallen.beginFall(7.0);
        const int64_t newFall = fallen.beginFall(7.0);
        CHECK(oldFall != newFall);
        CHECK(!fallen.tryAccept(oldFall));
        CHECK(fallen.tryAccept(newFall));
    }

    // A rescue with no open fall (never fell, or already rescued) is ignored.
    {
        FallTracker fallen(1);
        CHECK(!fallen.tryAccept(12345));
    }

    // A stale clear never removes a newer fall; entries also expire on their own.
    {
        PendingRescues rescuer;
        rescuer.add(1, 100, 6.0);
        rescuer.add(1, 200, 6.0); // newer fall replaces the older one
        CHECK(!rescuer.clear(1, 100));
        CHECK(rescuer.has(1));
        rescuer.tick(6.5);
        CHECK(rescuer.empty());
    }

    // Ids from different clients never collide.
    {
        FallTracker a(1), b(2);
        CHECK(a.beginFall(1.0) != b.beginFall(1.0));
    }

    // Two rescuers both SEND a rescue. Only the one the fallen player accepted is told it won; the other
    // is told it was too late, and a client that never tried gets NotMine.
    {
        FallTracker fallen(1);
        PendingRescues pendingB, pendingC;
        RescueAttempts attemptsB, attemptsC, attemptsD;
        const int64_t fall = fallen.beginFall(7.0);
        pendingB.add(1, fall, 6.0);
        pendingC.add(1, fall, 6.0);

        const auto b = pendingB.takeFirst();
        const auto c = pendingC.takeFirst();
        attemptsB.sent(b->fallId);
        attemptsC.sent(c->fallId);

        CHECK(fallen.tryAccept(b->fallId));   // B arrived first
        CHECK(!fallen.tryAccept(c->fallId));  // C is rejected by the fallen player

        // RescueAccepted(winner = 2) reaches everyone.
        CHECK(attemptsB.resolve(fall, 2, 2) == RescueAttempts::Result::Won);
        CHECK(attemptsC.resolve(fall, 2, 3) == RescueAttempts::Result::TooLate);
        CHECK(attemptsD.resolve(fall, 2, 4) == RescueAttempts::Result::NotMine);
        CHECK(attemptsB.resolve(fall, 2, 2) == RescueAttempts::Result::NotMine); // resolved once only
    }

    if (failures == 0)
        std::printf("rescue_tracker_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}