# Project 2 engine design

## Network messages

Shared packet: `Engine/include/NetMessage.h`.

`NetMessage` is packed and 29 bytes. It contains no SDL types, so the headless server can include it.

| Field | Meaning |
|---|---|
| `type` | `Join` = 1, `Leave` = 2, `PlayerState` = 3, `PlatformState` = 4 |
| `clientId` | Sender id assigned by the server, or chosen by the peer in peer-to-peer mode |
| `tic` | Sender timeline tic, not a frame count |
| `x`, `y` | Position in reference resolution |
| `velocityX`, `velocityY` | Velocity in reference units per second |

`PlayerState` is one player. `PlatformState` is the one shared moving platform.

## Client-server

Synchronous 0MQ. Each connection uses a `PAIR` or `REQ`/`REP` socket. One socket lives on one thread. `ROUTER` and `DEALER` are not used.

`--server` is a separate process. It does not construct `Engine`, because that constructor opens an SDL window. The accept loop hands each client its own thread. That thread blocks only on its own socket. Shared world state is protected by a mutex. The server steps the platform on `worldTime` and sends `PlatformState` on a fixed interval of the global timeline. Three clients can be connected at once. A client may join after the others, and one disconnect leaves the rest running.

`--client <host> <port>` is the windowed process. A network thread receives into a back buffer. Frame workers read the latest snapshot from that buffer. Events, input, and rendering stay on the main thread.

## Peer-to-peer

`--p2p <host:port> ...` is a later piece. It does not call the server. Peers exchange a start tic, keep the earliest as the shared anchor, and compute the platform from `(now - anchor)` and tic size. Player states go directly between peers. A message with an older tic than the one already applied is dropped.

## Launch flags

| Flag | Process |
|---|---|
| `--server` | Headless server |
| `--client <host> <port>` | Windowed client |
| `--p2p <host:port> ...` | Peer, no server |
