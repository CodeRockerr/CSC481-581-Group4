# Milestone 3 networking on the game object model

Owner: Sai. Builds on: `Design_ObjectModel.md` (Adit), `Design_P2P.md` (Adit), `Design_ClientThreads.md`.

## What changed and why

Milestone 2 sent one fixed 29-byte `NetMessage` per player and one per platform. Every new kind of
object (a second platform, an enemy, a projectile) needed a new message type and new code in
every game. Milestone 3 replicates **game objects** instead:

- Any `GameObject` with a `Transform` and a `NetworkIdentity` component is replicated.
- The receiver builds the replica with the same component model, so the server, every client and
  every peer run the same object model.
- Adding a new networked object type is one `NetworkIdentity(owner, kind)` line plus a spawn
  callback that attaches the look for that `kind`. No protocol change.

## Identity and ownership

| Field | Meaning |
|---|---|
| `ownerId` | Who is authoritative. `0` = server. Client-server: the client id assigned at Join. P2P: the peer's bind port. |
| `netId` | The object's id inside its owner. `0` means "use my local GameObject id". |
| `kind` | Game-defined tag (`NetKind`, games use 100+) that tells receivers what to build. |
| `isReplica` | Set on copies of someone else's object. Game code must not move these. |

The pair `(ownerId, netId)` is unique across the session (`netKey()` packs it into 64 bits).

**Why replicas are not created with `createWithId(netId)`** (the task list suggested it):
`createWithId` requires the id to be unused locally and moves the local id counter past it. That
breaks in our game as soon as two endpoints create objects:

- The server's first platform has id 1. Every client's background tile also has id 1, so
  `createWithId(1)` on a client throws "GameObject id 1 already exists".
- Client 1's ninja and client 2's ninja get the same local id on their own machines, so the ids
  are not even unique across the network.
- Reserving a high id range for replicas does not help: the first `createWithId(0x80000000)`
  pushes the counter so every later local `create()` lands in the replica range.

So identity on the wire is the pair `(ownerId, netId)`, and each endpoint keeps a
`(owner, netId) -> local id` map in `NetReplicator` and creates replicas with plain `create()`.
Game code never sees the difference: replicas are ordinary GameObjects with a `NetworkIdentity`
whose `isReplica` is true. If the team prefers shared ids, the alternative is a server-issued
global id space plus a `GameObjectManager` change so `createWithId` does not move the local
counter. That touches the object-model lifecycle (Adit's area), so it was not done unilaterally.

**Spoofing:** the server overwrites `owner` on everything a client sends with that client's id, so
a client can only ever update its own objects.

## Create / update / destroy lifecycle

The protocol replicates **full state snapshots**, and the lifecycle is implied by membership:

| Event | On the wire | On the receiver (`NetReplicator::sync`) |
|---|---|---|
| Create | an `(owner, netId)` appears in a snapshot for the first time | `create()` a GameObject, add Transform + Sprite + NetworkIdentity, call the game's spawn callback for its `kind` |
| Update | the same `(owner, netId)` appears again | write Transform, Sprite frame/flip/visible, active flag |
| Destroy | the `(owner, netId)` is missing from the next snapshot (owner left, crashed, or destroyed it) | `destroy(id)` (deferred, flushed by the engine after the frame workers) |

Why not separate Create/Destroy messages: with request/reply snapshots there is no "missed
destroy" problem. A lost or late packet is simply replaced by the next full snapshot, so endpoints
can never drift out of sync on which objects exist. The cost is that every snapshot carries every
object, which is exactly what the Part 1C experiments measure.

## Who owns what

- **Server-owned (owner 0):** moving platforms and anything else that must look the same for
  everyone. The server moves them in its scene update on a fixed 60 Hz simulation thread.
- **Client-owned:** each client's player. The client simulates its own player (immediate input
  response), sends its state, and the server relays it to everyone else.
- **P2P:** each peer owns its player. Shared moving platforms are not sent; every peer computes
  them from a shared clock anchor (the earliest start time any peer has seen), as in Milestone 2.

This is client-authoritative for players. It is simple and gives zero input lag on your own
character. The cost is that the server cannot prevent cheating (a client could teleport). For a
course engine with trusted clients, latency matters more; server-side validation could be added
in `Server::clientLoop` because the server already holds every player as a `GameObject`.

## Wire format

One 0MQ frame per packet. The first byte names the encoding, so one server serves both formats
at once and each client chooses its own.

```
PacketHeader (22 bytes, packed)        ObjectState (36 bytes, packed)
  uint8  format   'B' or 'T'             uint32 netId
  uint8  type     Join/JoinAccept/       uint32 owner
                  Leave/State/Snapshot   uint16 kind
  uint32 sender                          uint8  flags  (flip, visible, active)
  int64  tic                             uint8  frame  (sprite animation frame)
  uint32 aux      port / client count    float  x, y, width, height
  uint32 count    number of objects      float  velocityX, velocityY
```

- **Format A, Binary (`'B'`):** the header and `ObjectState` array are copied as raw bytes
  (`memcpy`). Size = 22 + 36·n. Decoding validates the exact length.
- **Format B, Text (`'T'`):** one header line then one line per object, numbers printed with
  `snprintf` (`%.3f` for floats) and parsed with `strtoul`/`strtof`. Roughly 55 to 60 bytes per
  object, and every number is formatted and parsed as characters.

Both carry the same fields with the same meaning, so gameplay is identical. Only the
representation changes. This is the "serialized text vs raw byte arrays" comparison for Part 1C.

Binary assumes both ends are little-endian with IEEE floats (true for every machine the team
uses). Text is portable and human-readable (useful when debugging with a packet dump).

## Message flow (client-server)

```
client                         server accept socket (5555)
  Join ('B' or 'T') ---------> assign client id, bind dedicated port 6000+id
  <------------- JoinAccept(sender = id, aux = port)

client network thread          server client thread (one per client)
  State(owned objects) ------> replaceOwner(id, objects)
  <--------- Snapshot(all server objects + all clients' objects, aux = client count)
  ... repeats every 16 ms (also a heartbeat) ...
  Leave ---------------------> forgetOwner(id); reply Leave; thread exits
```

## Threads

| Endpoint | Threads |
|---|---|
| Server | accept loop (caller of `run()`), one thread per client, one simulation thread |
| Client | game main thread + frame workers (engine), one network thread |
| Peer | accept thread, one thread per connected peer |

The game thread never blocks on the network. `publish()` copies owned object states into a
buffer; the network thread sends whatever is newest. Received snapshots are stored behind a mutex;
`sync()` applies the newest one on the game thread. Objects are only created or destroyed on the
thread that owns the `GameObjectManager` (rule from the object model design), so replicas are
created in `sync()`, never on a network thread.

## Disconnects

- **Graceful:** `NetworkClient::disconnect()` (also run by the destructor) sends `Leave`.
- **Crash or network loss:** the server client thread polls with a 250 ms receive timeout. If a
  client sends nothing for `clientTimeoutMs` (default 3 s, `--timeout-ms`), it is dropped.

Either way the server calls `forgetOwner(id)`. That client's objects are missing from the next
snapshot, and every other client's `sync()` destroys the replicas (via deferred `destroy()`), so
nothing else is interrupted. Finished client threads are joined and removed on the next accept.

## Measured behaviour (`net_sync_test`)

- 4 clients (2 binary, 2 text) each see 3 remote players + all server platforms.
- Player movement reaches another client in about 16-18 ms (one 60 Hz send interval). Target was < 100 ms.
- Graceful leave and silent crash (no `Leave`) both remove the player everywhere; the other three
  clients and the platforms keep running.
- Two peers (one binary, one text) replicate each other; a peer that quits disappears.

## Shared gameplay systems over the network

- Server-owned moving platforms carry Yeva's `MovementPath`; the server's scene update calls
  `GameplaySystems::updateMovementPaths`, and clients just receive the positions.
- Offline and P2P, each endpoint runs the same `MovementPath` locally. Peers set
  `MovementPath::elapsedTime` from the shared clock anchor before updating, so every peer's
  platforms line up without sending them.
- `SpawnPoint`, `DeathZone` and `SideScrollTrigger` are local to each client (each player has
  their own camera and their own respawns), so they are not replicated. The resulting player
  position is replicated like any other movement.
- Cave Ninja uses the shared `SpawnPoint`, `DeathZone` and `MovementPath`. Every death zone's
  `spawnPointName` is set to the latest checkpoint, so all deaths return the ninja to the last
  checkpoint it reached. For scrolling it uses two hidden boundary objects pinned to the view
  (smooth two-way scrolling) instead of `SideScrollTrigger`, which moves the camera by a fixed
  jump once and never moves it back after a respawn.

## Legacy compatibility

`NetMessage`, `NetworkClient::sendPlayerState/getLatestMessages` and
`PeerSession::sendPlayerState/getRemotePlayers` still exist as a deprecated view on top of the new
protocol, so games not yet moved to GameObjects still build and play. The plain `Server(port)` with
no scene runs the Milestone 2 moving platform. Remove these once every game uses
`publish()`/`sync()`.

## Using it in a game

```cpp
NetworkClient net(host, 5555, WireFormat::Binary);   // or WireFormat::Text
net.connect();

GameObject &me = objects.create("player");
me.add<Transform>(x, y, w, h);
me.add<Sprite>(texture, 4);
me.add<NetworkIdentity>(net.getClientId(), MyKinds::Player);

// every frame, in the game update callback:
net.sync(objects, [&](GameObject &replica, const ObjectState &s) {
    if (s.kind == MyKinds::Player) replica.get<Sprite>()->texture = texture;
});
// ... game logic ...
net.publish(objects);
```

Server with game-specific server objects: see `Games/skaveti/server.cpp`.

## Performance experiments (Part 1C)

- `net_bench_client`: headless client. One iteration = publish, wait for a fresh snapshot, apply it
  to a `GameObjectManager`. Prints elapsed time, bytes and round trip for N iterations.
- `engine_server --objects N`: benchmark scene with N moving objects (horizontal, vertical,
  circular paths).
- `Tools/net_experiments.sh`: runs E1 (10/100/500 objects), E2 (1/2/4 clients), E3 (1/100/500
  objects owned per client) for both formats, 5 repetitions, fresh server per run.
- `Tools/net_analyze.py`: mean and sample variance per case, `summary.md`, and one chart per
  experiment.
