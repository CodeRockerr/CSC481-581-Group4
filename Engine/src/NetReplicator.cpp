#include "NetReplicator.h"

#include <iterator>
#include <utility>

#include "Components.h"
#include "GameObject.h"
#include "NetworkIdentity.h"

bool NetReplicator::capture(const GameObject &object, ObjectState &out)
{
    const NetworkIdentity *identity = object.get<NetworkIdentity>();
    const Transform *transform = object.get<Transform>();
    if (identity == nullptr || transform == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(object.getMutex());
    out.netId = identity->netId != 0 ? identity->netId : object.getId();
    out.owner = identity->ownerId;
    out.kind = identity->kind;
    out.x = transform->x;
    out.y = transform->y;
    out.width = transform->width;
    out.height = transform->height;

    out.velocityX = 0.0f;
    out.velocityY = 0.0f;
    if (const PhysicsBody *body = object.get<PhysicsBody>())
    {
        out.velocityX = body->velocityX;
        out.velocityY = body->velocityY;
    }
    else if (identity->isReplica)
    {
        out.velocityX = identity->lastVelocityX;
        out.velocityY = identity->lastVelocityY;
    }

    out.flags = object.active ? NetFlags::Active : 0;
    out.frame = 0;
    if (const Sprite *sprite = object.get<Sprite>())
    {
        out.frame = static_cast<uint8_t>(sprite->frame);
        if (sprite->flipHorizontal)
        {
            out.flags |= NetFlags::FlipHorizontal;
        }
        if (sprite->visible)
        {
            out.flags |= NetFlags::Visible;
        }
    }
    else
    {
        out.flags |= NetFlags::Visible;
    }
    return true;
}

void NetReplicator::apply(GameObject &object, const ObjectState &state)
{
    std::lock_guard<std::mutex> lock(object.getMutex());
    if (Transform *transform = object.get<Transform>())
    {
        transform->x = state.x;
        transform->y = state.y;
        if (state.width > 0.0f)
        {
            transform->width = state.width;
        }
        if (state.height > 0.0f)
        {
            transform->height = state.height;
        }
    }
    if (Sprite *sprite = object.get<Sprite>())
    {
        sprite->frame = state.frame;
        sprite->flipHorizontal = (state.flags & NetFlags::FlipHorizontal) != 0;
        sprite->visible = (state.flags & NetFlags::Visible) != 0;
    }
    if (NetworkIdentity *identity = object.get<NetworkIdentity>())
    {
        identity->lastVelocityX = state.velocityX;
        identity->lastVelocityY = state.velocityY;
    }
    object.active = (state.flags & NetFlags::Active) != 0;
}

void NetReplicator::setLocalOwner(uint32_t owner)
{
    std::lock_guard<std::mutex> lock(mutex);
    localOwner = owner;
}

uint32_t NetReplicator::getLocalOwner() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return localOwner;
}

void NetReplicator::publish(const GameObjectManager &objects)
{
    const uint32_t owner = getLocalOwner();
    std::vector<ObjectState> states;
    for (const auto &object : objects.getAll())
    {
        const NetworkIdentity *identity = object->get<NetworkIdentity>();
        if (identity == nullptr || identity->isReplica || identity->ownerId != owner)
        {
            continue;
        }
        ObjectState state;
        if (capture(*object, state))
        {
            states.push_back(state);
        }
    }
    setOutgoing(std::move(states));
}

void NetReplicator::setOutgoing(std::vector<ObjectState> states)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (ObjectState &state : states)
    {
        state.owner = localOwner;
    }
    outgoing = std::move(states);
}

std::vector<ObjectState> NetReplicator::takeOutgoing()
{
    std::lock_guard<std::mutex> lock(mutex);
    return outgoing;
}

void NetReplicator::replaceAll(const std::vector<ObjectState> &states)
{
    std::lock_guard<std::mutex> lock(mutex);
    remote.clear();
    for (const ObjectState &state : states)
    {
        if (state.owner == localOwner && localOwner != 0)
        {
            continue;
        }
        remote[netKey(state.owner, state.netId)] = state;
    }
    ++remoteVersion;
}

void NetReplicator::replaceOwner(uint32_t owner, const std::vector<ObjectState> &states)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = remote.begin(); it != remote.end();)
    {
        it = (it->second.owner == owner) ? remote.erase(it) : std::next(it);
    }
    for (const ObjectState &state : states)
    {
        if (state.owner != owner)
        {
            continue; // a sender may only speak for its own objects
        }
        remote[netKey(state.owner, state.netId)] = state;
    }
    ++remoteVersion;
}

void NetReplicator::forgetOwner(uint32_t owner)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = remote.begin(); it != remote.end();)
    {
        it = (it->second.owner == owner) ? remote.erase(it) : std::next(it);
    }
    ++remoteVersion;
}

std::vector<ObjectState> NetReplicator::remoteStates() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<ObjectState> states;
    states.reserve(remote.size());
    for (const auto &entry : remote)
    {
        states.push_back(entry.second);
    }
    return states;
}

std::size_t NetReplicator::remoteCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return remote.size();
}

std::size_t NetReplicator::sync(GameObjectManager &objects, const SpawnFn &onSpawn, const DespawnFn &onDespawn)
{
    std::unordered_map<uint64_t, ObjectState> latest;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (remoteVersion == appliedVersion)
        {
            return replicaIds.size();
        }
        latest = remote;
        appliedVersion = remoteVersion;
    }

    // Destroy replicas whose owner stopped sending them (left, crashed or destroyed the object).
    for (auto it = replicaIds.begin(); it != replicaIds.end();)
    {
        if (latest.count(it->first) == 0)
        {
            if (GameObject *object = objects.find(it->second))
            {
                if (onDespawn)
                {
                    onDespawn(*object);
                }
                objects.destroy(it->second);
            }
            it = replicaIds.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Create new replicas and update existing ones.
    for (const auto &entry : latest)
    {
        const ObjectState &state = entry.second;
        GameObject *object = nullptr;
        auto found = replicaIds.find(entry.first);
        if (found != replicaIds.end())
        {
            object = objects.find(found->second);
        }
        if (object == nullptr)
        {
            GameObject &created = objects.create("replica");
            created.add<Transform>(state.x, state.y, state.width, state.height);
            created.add<Sprite>();
            NetworkIdentity &identity = created.add<NetworkIdentity>(state.owner, state.kind, state.netId);
            identity.isReplica = true;
            if (onSpawn)
            {
                onSpawn(created, state);
            }
            replicaIds[entry.first] = created.getId();
            object = &created;
        }
        apply(*object, state);
    }
    return replicaIds.size();
}
