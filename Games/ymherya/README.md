# Hello Kitty Adventure (Yeva)

Side-scrolling platformer for Milestone 3. Built on the team’s component `GameObject` model and object networking.

## Build

Requirements: CMake, a C++17 compiler. SDL3 is vendored. First configure may download libzmq.

```bash
git clone https://github.com/CodeRockerr/CSC481-581-Group4.git
cd CSC481-581-Group4
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ymherya_game ymherya_server -j8
```

Run every command from the **repository root**. Binaries are in `build/Release/`.

## How to run

**Offline (single player):**

```bash
./build/Release/ymherya_game
```

**Client–server (recommended for multiplayer):**

```bash
# Terminal 1
./build/Release/ymherya_server 5555

# Terminal 2 (and more clients as needed)
./build/Release/ymherya_game --client 127.0.0.1 5555
```

Optional wire format (default is binary):

```bash
./build/Release/ymherya_game --client 127.0.0.1 5555 binary
./build/Release/ymherya_game --client 127.0.0.1 5555 text
```

**Peer-to-peer (no server):**

```bash
./build/Release/ymherya_game --p2p 6001 127.0.0.1:6002
./build/Release/ymherya_game --p2p 6002 127.0.0.1:6001
```

## How to play

| Control | Action |
|---|---|
| **A** / **←** | Move left |
| **D** / **→** | Move right |
| **W** / **↑** / **Space** | Jump |
| **P** | Pause / unpause player timeline |
| **1** / **2** / **3** | Player timeline 0.5× / 1× / 2× |
| **Tab** | Toggle percentage / fixed scale mode |

**Goal:** Jump across pink clouds, avoid **Kuromi** on the ground, and don’t fall into pits.

- You have **3 lives** (hearts in the top-left). Touching Kuromi or falling into a pit costs a life and respawns you at a checkpoint.
- Moving clouds drift vertically, horizontally, or in a circle — you can ride them.
- The camera side-scrolls as you push past the left/right boundaries.
- In client mode, the **server** owns the moving clouds and Kuromi; other players appear as remote Hello Kitties.
