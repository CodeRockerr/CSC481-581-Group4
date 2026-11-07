#pragma once
#include "Event.h"
#include "GameObject.h"
#include "GameObjectManager.h"
#include "NetworkIdentity.h"
#include <cstdint>

namespace EventObjects
{
    // Reserved owner id marking an offline (local-id) reference. Never a real client/server id.
    inline constexpr std::uint32_t OfflineOwner = 0xFFFFFFFFu;

    inline EventObject of(const GameObject &object)
    {
        if (const NetworkIdentity *id = object.get<NetworkIdentity>())
            if (id->netId != 0)
                return {id->ownerId, id->netId};
        return {OfflineOwner, object.getId()};
    }

    inline bool isOffline(const EventObject &o) { return o.ownerId == OfflineOwner; }

    // The default-constructed EventObject: "no object".
    inline bool isNone(const EventObject &o) { return o.ownerId == 0 && o.netId == 0; }

    // Reverse lookup. Returns nullptr when the object no longer exists.
    inline GameObject *resolve(GameObjectManager &objects, const EventObject &ref)
    {
        if (isNone(ref))
            return nullptr;
        if (isOffline(ref))
            return objects.find(ref.netId); // local GameObject id
        GameObject *found = nullptr;
        objects.forEach<NetworkIdentity>([&](GameObject &object, NetworkIdentity &id)
                                         {
            if (!found && id.ownerId == ref.ownerId && id.netId == ref.netId)
                found = &object; });
        return found;
    }
}