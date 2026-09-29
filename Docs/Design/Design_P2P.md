# Project 2 engine design

## Peer-to-peer

Files: `Engine/include/PeerClock.h`, `Engine/include/PeerSession.h`, `Engine/src/PeerSession.cpp`.

`--p2p` does not start `engine_server` and does not elect one peer as the authority. `--server` and `--client` stay in the build.

Each peer reads `std::chrono::system_clock` in microseconds. `SDL_GetTicksNS` is process-local, so it is not used here. On join, the two peers exchange those tics and keep the earlier one as the shared anchor. Later exchanges can only move the anchor earlier. Every peer then places the grey platform with the same formula the server uses:

`x = 520 + 180 * sin((now - anchor) / 1_000_000)`, `y = 520`.

## Sockets

Synchronous 0MQ `REQ`/`REP` only. One socket per thread. `ROUTER` and `DEALER` are not used.

| Socket | Role |
|---|---|
| Public `REP` on the peer's own port | `Join` only. The reply carries this peer's anchor in `tic` and the private port in `x`. |
| Private `REP` bound to `tcp://*:0` | One thread per connected peer. Game traffic only. |
| `REQ` | The peer with the higher port dials. The lower port only accepts, so each pair has one connection. |

A failed dial retries, so peers can start in any order. Game traffic is two frames: `PlatformState` (`tic` is the sender's current anchor, not a platform position) and then `PlayerState`. `clientId` is that peer's bind port. A `PlayerState` with an older `tic` than the one already stored is dropped. `clientId` 0 means that peer has no local player yet.

## Demo

`engine_demo --p2p <bindPort> <host:port> ...`

The main thread still owns SDL, input, and drawing. It sends the local player with `PlayerTime`'s tic and creates remote players. The world worker sets the platform from `platformX()` / `platformY()` with velocity 0, and applies remote players the same way. `P`, `1`, and `3` change only the local player.

```bash
./build/Release/engine_demo --p2p 6001 127.0.0.1:6002 127.0.0.1:6003
./build/Release/engine_demo --p2p 6002 127.0.0.1:6001 127.0.0.1:6003
./build/Release/engine_demo --p2p 6003 127.0.0.1:6001 127.0.0.1:6002
