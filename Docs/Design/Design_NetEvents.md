# Networked Events (Milestone 4)

Owner: Sai. Builds on `Design_Events.md` (EventManager, Event, ReplayController).

## Model: server-centric routing, client-simulated movement

- A client raises an event locally. The local EventManager handles it at its next dispatch,
  so input stays responsive.
- If the event is `shareable`, `Origin::Local`, and not a replay control, `NetEventBridge`
  hands it to `NetworkClient::sendEvent`.
- The server validates it, puts it in arrival order, and appends it to the queue of every
  other client. The sender is not echoed its own event.
- Each recipient's `NetEventBridge::pump()` raises the events into its own EventManager as
  `Origin::Network`, `shareable = false`. They run in `dispatch()` on the main thread.
- Snapshots keep synchronizing transforms. Events say that something happened.

Why server-centric: it matches the existing REQ/REP server. One process decides the order, so
each recipient's delivery queue follows that order. Local dispatch still follows EventManager
timestamps and priorities. The server is also the place to enforce authority rules, using
`Server::setEventFilter`. The cost is one extra hop, and the server is a single point of failure.

## Wire format

`Packet` gained `events` and `eventAck`. They are written as a trailer after the object list,
only when there is something to send. A packet without events is byte-identical to the
Milestone 3 packet, so older tools still decode it.

| Part | Binary | Text |
| --- | --- | --- |
| Trailer header | `u64 ack, u32 count` | `E <ack> <count>` line |
| Each event | `u64 seq, u32 len, record` | one line: `<seq> <event tokens>` |
| Strings | `u32 length + bytes` | `~` + percent-encoding (empty string is `~`) |

`EventCodec` writes every field explicitly: type, timestamp, priority, id, source, target, and a
payload tag with the payload. `origin` and `shareable` are **not** sent; the receiver decides
them. Custom payloads travel as `EventFields` (int64, double, bool, string). Floats use `%.9g`
and doubles `%.17g` in text, so both formats round-trip exactly. Decoders reject truncation,
trailing bytes, unknown tags, type 0, and oversize strings (4096) or field maps (256).

## Reliability: per-link sequence numbers and cumulative acks

Each client<->server link has two independent streams, each numbered 1, 2, 3...

- **Client to server.** The client keeps events in `outgoingEvents` until the server's
  `eventAck` covers them, and resends all unacknowledged events (up to 64) on every exchange.
  The server accepts `seq == nextExpected`, drops `seq < nextExpected` as a duplicate, and
  stops at a gap. Acknowledging only the contiguous prefix means a lost reply or a request
  timeout loses nothing and applies nothing twice.
- **Server to client.** Each client has a pending queue on the server. Every reply carries the
  first 64 unacknowledged events plus the highest seq the server has received. The client
  accepts only `seq == nextInbound`, and its next request acknowledges it. The queue is
  released only on that ack.

Delivery is therefore exactly-once and in order per link, and queueing under one mutex makes
the order identical across recipients. Fan-out rides on the existing REQ/REP exchange, so
there are no unsolicited sends. Delivery latency is about one client send interval (16 ms
default) plus the round trip.

## Server authority

- The server overwrites `id.ownerId` with the authenticated client id. A client cannot forge
  another client's EventIds.
- Replay controls and type 0 are rejected. A rejected event is acknowledged, not resent.
- `setEventFilter(fn)` can veto an event (return false) for game-specific rules.
- `broadcastEvent(e)` assigns a new `(0, sequence)` EventId under the routing mutex and
  sends the same nonzero sequence to every client. This event identity is separate from
  each recipient's delivery sequence and survives retransmission unchanged.

## Identity and clocks

- Objects use `(ownerId, netId)`. The bridge preserves both fields unchanged by default:
  owner `0` means server-owned, a client ID means client-owned, and `{0, 0}` means no object.
  Use `EventObjects::of(object)` and `EventObjects::resolve(objects, reference)` rather than
  substituting local object IDs for network IDs. Locally owned network actors must have their
  actual client ID and a nonzero netId in `NetworkIdentity` before sharing events. Custom
  identity adapters remain available through `setObjectMappers`.
- The sender's timestamp is informational only. The receiving client re-stamps with its own
  EventManager clock, so a remote player's clock is never compared with a local one. Server
  arrival order defines ordering, and local arrival time defines when the event is handled.
- During replay playback, EventManager already rejects Network-origin gameplay events, and the
  bridge never publishes Replay-origin events. Network replay is out of scope.

## Spawn events and snapshot objects

One owner creates remote objects: the snapshot (`NetReplicator`). A networked Spawn event
must not create an object. It tells a client that an existing actor respawned at a named point,
and the receiving handler moves the existing replica. Games must not instantiate a replica from
a Spawn event.

The actor's owner also owns the Death-to-Spawn decision. `GameplayEvents::onDeath` skips
death rules and child spawns for actors marked `NetworkIdentity::isReplica`. Their owner
handles the death once and, when appropriate, shares one Spawn. A server-requested Death
aimed at a client-owned actor follows the same rule: that client generates the Spawn.
For server-owned actors, the server supplies the Spawn instead. Incoming Spawn handlers
still move existing replicas and call `onSpawned` once.

`NetReplicator` marks replicas automatically. Games that create replicas themselves must set
`isReplica = true` too. Keep `isReplica = false` for locally authoritative actors. Enable
`setShareLifecycleEvents(true)` on the owner's GameplayEvents instance to publish its lifecycle
events. Transport duplicate prevention covers repeated delivery of one event; ownership prevents
different clients from generating duplicate consequences with new event IDs.

Spawn locations travel by their `SpawnEvent::spawnPoint` name, which must be configured on
every client. A shared spawn omits its optional source reference when that point is offline;
it still names the actor by its network identity. Networked spawn-point references are preserved.
The actor itself must be networked. Other events carrying offline references remain local.

## Using it in a game

```cpp
NetworkClient client("127.0.0.1", 5555);
client.connect();
EventManager events(timeline);
NetEventBridge bridge(events, client);    // after connect(): sets the owner id

// per frame, on the main thread:
bridge.pump();       // network -> EventManager
events.dispatch();   // handlers run here

Event death;
death.type = EventTypes::Death;
death.shareable = true;                  // opt in to networking
death.target = EventObjects::of(player);
death.payload = DeathEvent{"lava"};
events.raise(death);
```

## Offline references, local replay and disconnects

**Offline references stay local.** `EventObjects::of()` marks an object with no network identity as
`{OfflineOwner, localId}`. That id only means something on the machine that made it, so such an event is
never sent. `NetEventBridge` keeps it local and counts it (`getLocalOnlyCount()`). The server also refuses
it if a client sends one anyway: the event is acknowledged, so the client stops resending it, but it is not
forwarded and counts as rejected. Network events that arrive carrying an offline reference are discarded.
Objects that have a `NetworkIdentity` use `{ownerId, netId}`, and `EventObjects::resolve()` finds them on the
receiving side.

**Local replay stays local.** Replay controls and events with `Origin::Replay` are never sent. While
`EventManager::isPlaybackMode()` is true, `NetEventBridge::pump()` discards network events instead of raising
them (`getDiscardedCount()`). They were already acknowledged, so they are dropped, not delivered late. A game
that supports replay while connected should also skip `sync()` and `publish()` during playback. Cave Ninja
does this. Its replay is offline only, so the guard is a safety net there.

**Disconnected clients.** When a client sends Leave, or is silent past the server's timeout, the server
releases its delivery queue (`getEventLinkCount()` follows `getClientCount()`). Events that client sent
earlier were already queued for the others and are still delivered. A broadcast after it left is queued only
for connected clients. A client that joins later gets a new id and a fresh link, starting at sequence 1, and
never sees events sent before it joined.

## Tests

`Tests/NetEventsTest.cpp` (`net_events_test`): codec round trips in both formats, truncation
and trailing-byte rejection, M3 packet size unchanged, loopback delivery in both wire
formats (identity, no echo, a 150-event burst in order and once, non-shareable and
replay events stay local, filter rejection, both directions, server broadcast), and a raw-socket test of
retransmission, gaps, redelivery until ack, and spoofed owner ids.

Gameplay integration tests run in both formats. They resolve client-owned and server-owned
actors through the real handlers, preserve empty references, compare stable server EventIds
across clients, and verify that local and server-requested deaths cause one spawn per client.
They also cover offline and networked spawn points and server-authored spawns for replicas.

```sh
cmake --build build --target net_events_test
ctest --test-dir build -R net_events_test --output-on-failure
```

## Limits

- Go-back-N resend wastes bandwidth under heavy loss. Fine for LAN, but not tuned.
- Events reach every other client; there is no interest filtering or per-room routing.
- The cap is 64 events per packet per direction. A larger burst drains over several exchanges.
- A client that times out loses its queue (it rejoins with a fresh link) and misses events sent while it was away.
- Server-side authority is a hook, not a rule set. Games supply the rules.
