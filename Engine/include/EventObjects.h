#pragma once
#include "Event.h"
#include "GameObject.h"
#include "GameObjectManager.h"
#include "NetworkIdentity.h"

namespace EventObjects
{
    // Networked objects (netId assigned) -> {ownerId, netId}.
    // Offline / not-yet-replicated objects  -> {0, localObjectId}.
    inline EventObject of(const GameObject &object)
    {
        if (const NetworkIdentity *id = object.get<NetworkIdentity>())
            if (id->netId != 0)
                return {id->ownerId, id->netId};
        return {0, object.getId()};
    }

    inline bool isNone(const EventObject &o) { return o.ownerId == 0 && o.netId == 0; }

    // Reverse lookup. Returns nullptr when the object no longer exists.
    inline GameObject *resolve(GameObjectManager &objects, const EventObject &ref)
    {
        if (isNone(ref))
            return nullptr;
        if (ref.ownerId == 0)
            return objects.find(ref.netId);
        GameObject *found = nullptr;
        objects.forEach<NetworkIdentity>([&](GameObject &object, NetworkIdentity &id)
                                         {
            if (!found && id.ownerId == ref.ownerId && id.netId == ref.netId)
                found = &object; });
        return found;
    }
}