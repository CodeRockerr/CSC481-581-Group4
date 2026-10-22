#include "SeaLevel.h"
#include "SeaGameplay.h"
#include "SeaVisuals.h"

#include "Components.h"
#include "DeathZone.h"
#include "Engine.h"
#include "GamePlaySystems.h"
#include "Image.h"
#include "MovementPath.h"
#include "NetReplicator.h"
#include "NetworkClient.h"
#include "NetworkIdentity.h"
#include "PeerSession.h"
#include "SpawnPoint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    bool isChromaMagenta(Uint8 r, Uint8 g, Uint8 b)
    {
        return r >= 140 && b >= 140 && g <= 110 && (r - g) > 50 && (b - g) > 50;
    }

    void applyMagentaKey(SDL_Surface *surface)
    {
        if (!SDL_LockSurface(surface))
            return;

        Uint8 *pixels = static_cast<Uint8 *>(surface->pixels);
        for (int y = 0; y < surface->h; ++y)
        {
            Uint8 *row = pixels + y * surface->pitch;
            for (int x = 0; x < surface->w; ++x)
            {
                Uint8 *px = row + x * 4;
                if (isChromaMagenta(px[0], px[1], px[2]))
                    px[3] = 0;
            }
        }

        SDL_UnlockSurface(surface);
    }

    SDL_Texture *loadTexture(SDL_Renderer *renderer, const char *path, bool chromaKey, int frames = 1)
    {
        SDL_Surface *surface = loadImage(path);
        if (!surface)
        {
            const std::string name = std::string(path).substr(std::string(path).find_last_of('/') + 1);
            const char *basePath = SDL_GetBasePath();
            const std::string base = basePath ? basePath : "";
            for (const std::string &candidate : {"assets/" + name, base + "assets/" + name,
                                                 base + "../../Games/ashah/assets/" + name})
            {
                surface = loadImage(candidate.c_str());
                if (surface)
                    break;
            }
        }
        if (!surface)
        {
            throw std::runtime_error(std::string("Could not load ") + path + ": " + SDL_GetError());
        }

        if (chromaKey)
        {
            applyMagentaKey(surface);
            // Trim a common rectangle from every frame, preserving animation alignment.
            const int frameWidth = surface->w / frames;
            int left = frameWidth, top = surface->h, right = 0, bottom = 0;
            const auto *pixels = static_cast<const Uint8 *>(surface->pixels);
            for (int y = 0; y < surface->h; ++y)
                for (int x = 0; x < surface->w; ++x)
                    if (pixels[y * surface->pitch + x * 4 + 3] > 32)
                    {
                        left = std::min(left, x % frameWidth);
                        right = std::max(right, x % frameWidth + 1);
                        top = std::min(top, y);
                        bottom = std::max(bottom, y + 1);
                    }
            if (right > left && bottom > top)
            {
                SDL_Surface *trimmed = SDL_CreateSurface((right - left) * frames, bottom - top,
                                                         SDL_PIXELFORMAT_RGBA32);
                if (!trimmed)
                    throw std::runtime_error(SDL_GetError());
                SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
                for (int frame = 0; frame < frames; ++frame)
                {
                    SDL_Rect src{frame * frameWidth + left, top, right - left, bottom - top};
                    SDL_Rect dst{frame * (right - left), 0, right - left, bottom - top};
                    SDL_BlitSurface(surface, &src, trimmed, &dst);
                }
                SDL_DestroySurface(surface);
                surface = trimmed;
            }
        }

        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (!texture)
        {
            throw std::runtime_error(std::string("Could not create texture for ") + path + ": " + SDL_GetError());
        }

        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        return texture;
    }

    SDL_Texture *makeSuitSheet(SDL_Renderer *renderer, SDL_Texture *normal, SDL_Texture *alternate,
                               bool swimming = false)
    {
        // The drawing canvas has room for fins; the gameplay body stays 96x80.
        SDL_Texture *sheet = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                               SDL_TEXTUREACCESS_TARGET, 8 * 256, 200);
        if (!sheet)
            throw std::runtime_error(SDL_GetError());
        SDL_SetTextureBlendMode(sheet, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(sheet, SDL_SCALEMODE_NEAREST);
        SDL_SetRenderTarget(renderer, sheet);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        for (int i = 0; i < 8; ++i)
        {
            SDL_Texture *source = i < 4 ? normal : alternate;
            float width, height;
            SDL_GetTextureSize(source, &width, &height);
            width /= 4;
            const float angle = swimming && i < 4 ? -30.f : 0.f;
            const float sine = angle ? .5f : 0.f, cosine = angle ? .8660254f : 1.f;
            const float maxWidth = angle ? 256.f : 192.f, maxHeight = angle ? 200.f : 160.f;
            const float scale = std::min(maxWidth / (width * cosine + height * sine),
                                         maxHeight / (height * cosine + width * sine));
            const float rotatedHeight = (height * cosine + width * sine) * scale;
            SDL_FRect src{(i % 4) * width, 0, width, height};
            SDL_FRect dst{i * 256.0f + (256 - width * scale) * 0.5f,
                          200 - rotatedHeight * .5f - height * scale * .5f,
                          width * scale, height * scale};
            SDL_RenderTextureRotated(renderer, source, &src, &dst, angle, nullptr, SDL_FLIP_NONE);
        }
        SDL_SetRenderTarget(renderer, nullptr);
        return sheet;
    }

    struct Options
    {
        std::string host;
        uint16_t port = 5555;
        WireFormat format = WireFormat::Binary;
        uint16_t peerPort = 0;
        bool smokeTest = false;
        bool verifyRun = false;
        bool verifyPause = false;
        bool verifyFoundations = false;
        double speed = 1;
        std::string screenshot;
        int previewCheckpoint = 0;
        std::vector<std::pair<std::string, uint16_t>> peers;
    };

    bool parseArgs(int argc, char *argv[], Options &options)
    {
        auto portNumber = [](const std::string &text)
        {
            std::size_t used = 0;
            int value = std::stoi(text, &used);
            if (used != text.size() || value < 1 || value > 65535)
                throw std::runtime_error("Port must be between 1 and 65535");
            return static_cast<uint16_t>(value);
        };
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--smoke-test" || arg == "--verify-run" || arg == "--verify-pause" || arg == "--verify-foundations")
            {
                options.smokeTest = arg == "--smoke-test";
                options.verifyRun = arg == "--verify-run";
                options.verifyPause = arg == "--verify-pause";
                options.verifyFoundations = arg == "--verify-foundations";
                if (i + 1 < argc && std::string(argv[i + 1]).find("--") != 0)
                    options.screenshot = argv[++i];
                if (i + 1 < argc && std::string(argv[i + 1]).find("--") != 0)
                    options.previewCheckpoint = std::clamp(std::stoi(argv[++i]), 0, 9);
            }
            else if (arg == "--speed" && i + 1 < argc)
            {
                const std::string value = argv[++i];
                if (value != "0.5" && value != "1" && value != "2")
                    throw std::runtime_error("Speed must be 0.5, 1, or 2");
                options.speed = std::stod(value);
            }
            else if (arg == "--client" && i + 1 < argc)
            {
                options.host = argv[++i];
                if (i + 1 < argc && std::string(argv[i + 1]).find("--") != 0)
                    options.port = portNumber(argv[++i]);
                if (i + 1 < argc && std::string(argv[i + 1]).find("--") != 0 &&
                    !NetProtocol::parseFormat(argv[++i], options.format))
                    throw std::runtime_error("Format must be binary or text");
            }
            else if (arg == "--p2p" && i + 1 < argc)
            {
                options.peerPort = portNumber(argv[++i]);
                while (i + 1 < argc && std::string(argv[i + 1]).find("--") != 0)
                {
                    const std::string peer = argv[++i];
                    const auto colon = peer.rfind(':');
                    if (colon == std::string::npos)
                        throw std::runtime_error("Expected peer host:port");
                    options.peers.emplace_back(peer.substr(0, colon), portNumber(peer.substr(colon + 1)));
                }
            }
            else
                throw std::runtime_error("Usage: ashah_game [--client HOST [PORT [binary|text]]] or [--p2p PORT HOST:PORT ...]");
        }
        if (!options.host.empty() && options.peerPort != 0)
            throw std::runtime_error("Choose either client-server or peer-to-peer mode");
        return true;
    }
}

int runGame(int argc, char *argv[])
{
    Options options;
    parseArgs(argc, argv, options);
    std::unique_ptr<NetworkClient> net;
    if (!options.host.empty())
    {
        net = std::make_unique<NetworkClient>(options.host.c_str(), options.port, options.format);
        if (!net->connect())
            throw std::runtime_error("Could not connect to server");
    }
    std::unique_ptr<PeerSession> peers;
    if (options.peerPort)
    {
        peers = std::make_unique<PeerSession>(options.peerPort, options.format);
        for (const auto &p : options.peers)
            peers->connectTo(p.first, p.second);
    }
    const uint32_t owner = net ? net->getClientId() : peers ? peers->getOwnerId()
                                                            : 0;
    Engine engine("Lost Under the Sea | ABYSS RUN", 1920, 1080);
    auto &objects = engine.getObjects();
    auto &camera = engine.getCamera();
    auto &physics = engine.getPhysics();
    physics.setGravity(Sea::Gravity);
    auto *renderer = engine.getRenderer().getHandle();
    objects.setReferenceResolution(1280, 720);
    objects.setScaleMode(ScaleMode::Percentage);
    camera.setViewport(1280, 720);
    camera.setBounds(0, 0, Sea::WorldWidth, Sea::WorldHeight);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    std::vector<SDL_Texture *> textures;
    int assetCount = 0;
    auto load = [&](const char *name, int frames = 1)
    {
        const std::string path = "Games/ashah/assets/" + std::string(name);
        auto *texture = loadTexture(renderer, path.c_str(), std::string(name) != "background.png", frames);
        textures.push_back(texture);
        ++assetCount;
        return texture;
    };
    auto keep = [&](SDL_Texture *t)
    { textures.push_back(t); return t; };
    auto *background = load("background.png");
    auto *redSuit = load("diver.png", 4);
    auto *yellowSuit = load("diver_alt.png", 4);
    auto *suits = keep(makeSuitSheet(renderer, redSuit, yellowSuit));
    auto *fishArt = load("fish.png", 4);
    auto *anglerArt = load("anglerfish.png", 4);
    auto *bubbleArt = load("bubble.png", 3);
    auto *pearlArt = load("pearl.png");
    auto *buoyArt = load("buoy.png");
    SDL_Texture *platformArt[] = {
        load("platform_coral.png"), load("platform_wreck.png"), load("platform_kelp.png"),
        load("sheet_coral.png"), load("sheet_wreck.png"), load("sheet_kelp.png"),
        load("sheet_current.png"), load("shelf.png")};
    auto *glow = keep(SeaVisuals::glow(renderer));
    auto *powerArt = keep(SeaVisuals::power(renderer));
    auto *diverBodyArt = keep(SeaVisuals::target(renderer, 16, 1));
    SDL_SetRenderTarget(renderer, nullptr);
    auto makeSprite = [&](const char *name, float x, float y, float w, float h,
                          SDL_Texture *texture, int layer, int frames = 1) -> GameObject &
    {
        auto &o = objects.create(name);
        o.add<Transform>(x, y, w, h);
        auto &s = o.add<Sprite>(texture, frames);
        s.layer = layer;
        return o;
    };
    auto optionalArt = [&](const char *name, bool transparent, int frames = 1) -> SDL_Texture *
    {
        const std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
        for (const auto &path : {"Games/ashah/assets/" + std::string(name), "assets/" + std::string(name),
                                 base + "assets/" + name, base + "../../Games/ashah/assets/" + name})
            if (std::filesystem::exists(path))
            {
                ++assetCount;
                return keep(loadTexture(renderer, path.c_str(), transparent, frames));
            }
        return nullptr;
    };
    auto *swimmingSuits = suits;
    if (auto *swimFrames = optionalArt("diver_swim.png", true, 4))
        swimmingSuits = keep(makeSuitSheet(renderer, swimFrames, yellowSuit, true));
    auto *shoreArt = optionalArt("background_shore.png", false);
    const bool paintedShore = shoreArt != nullptr;
    if (!shoreArt)
        shoreArt = keep(SeaVisuals::shore(renderer));
    // The supplied image's near waterline is 60% down its canvas, rather
    // than at the requested 520/720. Match that line to the physical surface.
    makeSprite("shore-and-sky", 0, 0, 1280, paintedShore ? Sea::SurfaceY / .60f : 720, shoreArt, -21);
    const char *regionFiles[] = {"background_coral.png", "background_wreck.png", "background_abyss.png"};
    for (int i = 0; i < 3; ++i)
    {
        auto *regionArt = optionalArt(regionFiles[i], false);
        if (!regionArt)
            regionArt = keep(SeaVisuals::regionBackdrop(renderer, background, i));
        const int overlap = i ? 80 : 0;
        const int height = static_cast<int>(Sea::RegionHeights[i]) + overlap + (i < 2 ? 80 : 0);
        auto *blended = keep(SeaVisuals::blendRegion(renderer, regionArt, height, i > 0));
        makeSprite("region-backdrop", 0, Sea::RegionStarts[i] - overlap, 1280,
                   static_cast<float>(height), blended, -20 + i);
    }
    auto *waveArt = keep(SeaVisuals::target(renderer, 1280, 20));
    SDL_SetRenderTarget(renderer, nullptr);
    auto &waves = makeSprite("waterline", 0, Sea::SurfaceY - 7, 1280, 20, waveArt, 8);
    auto *newBuoy = optionalArt("buoy_surface.png", true);
    if (newBuoy)
        buoyArt = newBuoy;
    auto *titleArt = optionalArt("title_logo.png", false);
    auto *shoreDock = optionalArt("shore_dock.png", true);
    for (int i = 0; i < Sea::StaticPlatformCount; ++i)
    {
        const auto &d = Sea::StaticPlatforms[i];
        // This dock includes tall rope posts above its flat planks.
        const float topPad = i == 0 && shoreDock ? .31f : Sea::topPadding(d.art);
        auto &o = makeSprite(d.label, d.x, d.solidTopY - d.height * topPad, d.width, d.height,
                             i == 0 && shoreDock ? shoreDock : platformArt[static_cast<int>(d.art)], 0);
        o.add<Collider>(Sea::PlatformSidePad, Sea::PlatformSidePad, topPad, 0);
        o.add<Sea::Ground>();
        if (i == 0)
            o.add<Sea::Shore>();
        if (i == Sea::StaticPlatformCount - 1)
            o.add<Sea::RecoveryBed>();
    }
    auto dressMover = [&](GameObject &o, int i)
    {
        const auto &d = Sea::Movers[i];
        auto *s = o.get<Sprite>();
        if (!s)
            s = &o.add<Sprite>();
        s->layer = 2;
        if (d.kind == Sea::KindMovingShelf)
        {
            s->texture = platformArt[static_cast<int>(d.art)];
            o.add<Collider>(Sea::PlatformSidePad, Sea::PlatformSidePad, Sea::topPadding(d.art), 0);
            o.add<Sea::Ground>(i);
        }
        else
        {
            s->texture = d.kind == Sea::KindFish ? fishArt : anglerArt;
            s->frameCount = 4;
            o.add<Collider>(.15f, .15f, .15f, .15f, true);
            o.add<Sea::Hazard>();
        }
    };
    if (!net)
        for (int i = 0; i < Sea::MoverCount; ++i)
        {
            const auto &d = Sea::Movers[i];
            float x, y;
            Sea::moverStart(i, x, y);
            auto &o = objects.create("world-mover");
            o.add<Transform>(x, y, d.width, d.height);
            o.add<MovementPath>(d.path, d.speed, d.amplitude);
            dressMover(o, i);
        }
    for (const auto &d : Sea::Spawns)
    {
        auto &o = objects.create("hidden-spawn");
        o.add<Transform>(d.x, d.y, 24, 24);
        o.add<SpawnPoint>(d.name);
    }
    auto &pit = objects.create("hidden-death-zone");
    pit.add<Transform>(0, Sea::WorldHeight - 60, Sea::WorldWidth, 10000);
    pit.add<Collider>(0, 0, 0, 0, true);
    pit.add<DeathZone>(Sea::Spawns[0].name);
    std::vector<uint32_t> pearlIds, powerIds;
    for (const auto &d : Sea::Pearls)
    {
        auto &o = makeSprite("pearl", d.x, d.y, d.size, d.size, pearlArt, 3);
        o.add<Collider>(.05f, .05f, .05f, .05f, true);
        o.add<Sea::Pearl>();
        pearlIds.push_back(o.getId());
    }
    for (const auto &d : Sea::Powers)
    {
        auto &o = makeSprite("overdrive-core", d.x, d.y, 64, 64, powerArt, 3);
        o.add<Collider>(.05f, .05f, .05f, .05f, true);
        o.add<Sea::PowerUp>();
        powerIds.push_back(o.getId());
    }
    for (const auto &d : Sea::Urchins)
    {
        auto *art = keep(SeaVisuals::urchins(renderer, static_cast<int>(d.width)));
        auto &o = makeSprite("urchin-cluster", d.x(), d.y(), d.width, Sea::UrchinHeight, art, 2);
        o.add<Collider>(.02f, .02f, .08f, 0, true);
        o.add<Sea::Hazard>();
        o.add<Sea::Urchin>();
    }
    auto &buoy = makeSprite("surface-buoy", Sea::BuoyX, Sea::BuoyY,
                            Sea::BuoyWidth, Sea::BuoyHeight, buoyArt, 4);
    auto &gateGlow = makeSprite("buoy-opening", Sea::GateLeft, Sea::GateY - 100,
                                Sea::GateRight - Sea::GateLeft, 120, glow, 1);

    auto &player = makeSprite("captain", Sea::Spawns[0].x + (owner % 4) * 22,
                              Sea::Spawns[0].y, Sea::DiverWidth, Sea::DiverHeight, diverBodyArt, 6, 16);
    player.add<Collider>(.25f, .25f, .08f, 0);
    // Stepped in small increments by the shared Physics/manager APIs on the motion
    // worker. -1 keeps Engine's automatic workers from integrating it a second time.
    player.add<PhysicsBody>(-1, true);
    player.add<PlayerInput>(Sea::MoveSpeed, Sea::JumpSpeed);
    if (net || peers)
        player.add<NetworkIdentity>(owner, Sea::KindDiver);
    const auto playerId = player.getId();
    std::unordered_map<uint32_t, uint32_t> diverSkins;
    const NetReplicator::SpawnFn onSpawn = [&](GameObject &o, const ObjectState &state)
    {
        if (state.kind == Sea::KindDiver)
        {
            auto *s = o.get<Sprite>();
            s->texture = diverBodyArt;
            s->frameCount = 16;
            s->layer = 5;
            o.add<Collider>(.25f, .25f, .08f, 0);
        }
        else if (state.netId >= 1 && state.netId <= Sea::MoverCount)
            dressMover(o, static_cast<int>(state.netId - 1));
    };
    auto &shield = makeSprite("overdrive-aura", 0, 0, 140, 140, glow, 4);
    shield.get<Sprite>()->color = {255, 215, 95, 180};
    std::vector<uint32_t> bubbles;
    for (int i = 0; i < 48; ++i)
    {
        auto &o = makeSprite("rising-bubble", 50 + (i * 149 % 1180), i * 87.f, 13, 13, bubbleArt, -5, 3);
        o.get<Sprite>()->color = {130, 210, 230, 95};
        bubbles.push_back(o.getId());
    }
    std::vector<uint32_t> trail;
    std::vector<uint32_t> swimTrail;
    for (int i = 0; i < 8; ++i)
    {
        auto &o = makeSprite("overdrive-spark", 0, 0, 14, 14, glow, 4);
        o.get<Sprite>()->color = {255, 218, 113, 190};
        trail.push_back(o.getId());
        auto &bubble = makeSprite("swim-trail", 0, 0, 14, 14, bubbleArt, 4, 3);
        swimTrail.push_back(bubble.getId());
    }
    auto *hudTexture = keep(SeaVisuals::target(renderer, 1280, 720));
    SDL_SetRenderTarget(renderer, nullptr);
    auto &hud = makeSprite("screen-overlay", 0, 0, 1280, 720, hudTexture, 1000);
    auto &playerTime = engine.getTimeline(Engine::PlayerTime);
    auto &worldTime = engine.getTimeline(Engine::WorldTime);
    auto &globalTime = engine.getGlobalTimeline();
    playerTime.setScale(options.speed);
    Sea::Progress progress;
    Sea::PlayerMotion motion;
    bool titleScreen = (!options.smokeTest && !options.verifyRun && !options.verifyPause && !options.verifyFoundations && !net && !peers) ||
                       (options.smokeTest && options.previewCheckpoint == 7);
    std::unordered_map<uint32_t, SDL_FPoint> previousGround, previousFish;
    std::string message = "Jump off the shore. Recover " + std::to_string(Sea::PearlCount) + " pearls in 3 regions, then return to the SURFACE buoy.";
    float messageTime = 6;
    int frames = 0, testPhase = 0, respawnCount = 0;
    int lastVerifiedPearls = -1;
    int routeStage = 0, routeTarget = -1, leavingPearl = -1;
    bool safeAscent = false;
    bool verifiedFishShield = false, verifiedUrchinShield = false;
    bool verificationFinished = false;
    float sceneSeconds = 0;
    bool wasPaused = false;
    if (options.verifyRun)
        playerTime.setScale(2);
    auto say = [&](const std::string &s, float duration = 4)
    { message = s; messageTime = duration; };
    auto respawn = [&]()
    {
        ++respawnCount;
        auto &t = *player.get<Transform>();
        auto &b = *player.get<PhysicsBody>();
        t.x = Sea::Spawns[progress.checkpoint].x;
        t.y = Sea::Spawns[progress.checkpoint].y;
        b.velocityX = b.velocityY = 0;
        motion = {};
        progress.graceTime = 1.8f;
        camera.setPosition(0, t.y - 250);
        pit.get<DeathZone>()->armed = true;
    };
    auto restart = [&]()
    {
        progress = {};
        Sea::restoreCollectibles(objects);
        playerTime.unpause();
        respawn();
        say("Fresh dive. Jump into the sea, recover every pearl, then return to the buoy above.", 7);
    };
    auto endDive = [&]()
    {
        player.get<PhysicsBody>()->velocityX = player.get<PhysicsBody>()->velocityY = 0;
    };
    if (options.smokeTest)
    {
        if (options.previewCheckpoint < 3 || options.previewCheckpoint == 7)
        {
            progress.checkpoint = options.previewCheckpoint == 7 ? 0 : options.previewCheckpoint;
            respawn();
        }
        else if (options.previewCheckpoint <= 4)
        {
            auto &t = *player.get<Transform>();
            t.x = 972;
            t.y = Sea::SurfaceY - Sea::DiverHeight - 25;
            progress.pearls.fill(true);
            progress.reachedBottom = progress.returning = true;
            for (auto id : pearlIds)
                objects.find(id)->active = false;
            progress.won = options.previewCheckpoint == 4;
            if (progress.won)
                t.y = Sea::GateY - Sea::DiverHeight - 2;
            camera.setPosition(0, 0);
            say("Swim UP through the center of the surface buoy to finish the recovery.", 10);
        }
        else
        {
            auto &t = *player.get<Transform>();
            t.x = options.previewCheckpoint == 5 ? 550 : options.previewCheckpoint == 8 ? 680
                                                                                        : 230;
            t.y = options.previewCheckpoint == 5 ? 840 : options.previewCheckpoint == 8 ? 2714
                                                                                        : 920 - Sea::DiverHeight;
            if (options.previewCheckpoint == 9)
            {
                progress.health = 0;
                progress.gameOver = true;
                endDive();
            }
            progress.graceTime = 0;
            camera.setPosition(0, t.y - 420);
        }
    }
    if (options.verifyPause)
    {
        auto &t = *player.get<Transform>();
        t.x = 680;
        t.y = 2714;
        progress.activatePower();
        camera.setPosition(0, t.y - 420);
    }
    auto capture = [&](const std::string &suffix = "")
    {
        if (options.screenshot.empty())
            return;
        std::vector<RenderItem> snapshot;
        objects.copySnapshot(snapshot);
        engine.getRenderer().clear();
        objects.drawSnapshot(snapshot, renderer, engine.getWindow().getWidth(), engine.getWindow().getHeight(), camera);
        auto *image = SDL_RenderReadPixels(renderer, nullptr);
        if (!image || !SDL_SaveBMP(image, (options.screenshot + suffix).c_str()))
            throw std::runtime_error(SDL_GetError());
        SDL_DestroySurface(image);
    };
    auto quit = [&]()
    { SDL_Event e{}; e.type = SDL_EVENT_QUIT; SDL_PushEvent(&e); };
    // Compare the actual rendered world, excluding the pause overlay itself.
    auto sceneSnapshot = [&]()
    {
        std::vector<RenderItem> result;
        objects.copySnapshot(result);
        result.erase(std::remove_if(result.begin(), result.end(), [](const RenderItem &r)
                                    { return r.layer == 1000; }),
                     result.end());
        return result;
    };
    auto sameScene = [](const std::vector<RenderItem> &a, const std::vector<RenderItem> &b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            const auto &x = a[i];
            const auto &y = b[i];
            if (x.x != y.x || x.y != y.y || x.width != y.width || x.height != y.height ||
                x.texture != y.texture || x.frame != y.frame || x.frameCount != y.frameCount ||
                x.flipHorizontal != y.flipHorizontal || x.visible != y.visible || x.active != y.active ||
                x.color.r != y.color.r || x.color.g != y.color.g || x.color.b != y.color.b || x.color.a != y.color.a)
                return false;
        }
        return true;
    };
    std::vector<RenderItem> pausedScene;
    Sea::Progress pausedProgress;
    float pausedSeconds = 0, pausedMessageTime = 0;
    SDL_FPoint pausedCamera{}, pausedVelocity{};
    std::function<void()> motionStep;
    std::thread::id motionThread;
    const auto mainThread = std::this_thread::get_id();
    FrameWorker motionWorker([&]()
                             {
        motionThread = std::this_thread::get_id();
        motionStep(); });
    const Uint64 testStarted = SDL_GetTicksNS();
    const uint64_t initialMessages = net ? net->getMessagesSent() : 0;
    bool smokeReported = false;

    auto updateView = [&]()
    {
        const bool proportional = objects.getScaleMode() == ScaleMode::Percentage;
        const float w = proportional ? Sea::ViewWidth : engine.getWindow().getWidth();
        const float h = proportional ? Sea::ViewHeight : engine.getWindow().getHeight();
        camera.setViewport(w, h);
        // Keep the HUD readable in either mode; world sprites retain their pixel
        // size in Pixel mode and scale with the window in Percentage mode.
        hud.get<Transform>()->width = w;
        hud.get<Transform>()->height = h;
    };

    engine.run([&](float)
               {
        ++frames;
        const bool scriptedScale = options.verifyFoundations && (frames == 5 || frames == 15 || frames == 75 || frames == 85);
        if (Input::isKeyJustPressed(SDL_SCANCODE_F) || scriptedScale)
        {
            objects.toggleScaleMode();
            say(objects.getScaleMode() == ScaleMode::Pixel ? "PIXEL VIEW: objects keep their pixel size when the window resizes." :
                "PROPORTIONAL VIEW: the whole scene scales with the window.");
        }
        if (options.verifyFoundations)
        {
            if (frames == 10) engine.getWindow().resizeWindow(1024, 576);
            if (frames == 80) engine.getWindow().resizeWindow(800, 450);
            if (frames == 20) playerTime.setScale(.5);
            if (frames == 40) playerTime.setScale(1);
            if (frames == 60) playerTime.setScale(2);
        }
        updateView();
        if (net) net->setSendIntervalMs(static_cast<int>(std::lround(1000.0 / (60 * playerTime.getScale()))));
        if (options.verifyPause && (frames == 20 || frames == 100))
        {
            // P toggles this same shared-engine timeline before the game update.
            playerTime.pause();
            pausedScene = sceneSnapshot(); pausedProgress = progress;
            pausedSeconds = sceneSeconds; pausedMessageTime = messageTime;
            pausedCamera = {camera.getX(), camera.getY()};
            auto *body = player.get<PhysicsBody>();
            pausedVelocity = {body->velocityX, body->velocityY};
        }
        if (options.verifyPause && (frames == 80 || frames == 160)) playerTime.unpause();
        if (titleScreen && (Input::isKeyJustPressed(SDL_SCANCODE_RETURN) || Input::isKeyJustPressed(SDL_SCANCODE_SPACE)))
            titleScreen = false;
        if (Input::isKeyJustPressed(SDL_SCANCODE_R)) restart();
        if (Input::isKeyJustPressed(SDL_SCANCODE_ESCAPE)) quit();
        if (playerTime.isPaused())
        {
            worldTime.pause();
            wasPaused = true;
            SeaVisuals::hud(renderer, hudTexture, progress, player.get<Transform>()->y, sceneSeconds,
                            message, true, pearlArt, bubbleArt, titleArt, titleScreen);
            if (options.verifyPause)
            {
                auto *body = player.get<PhysicsBody>();
                if (!sameScene(pausedScene, sceneSnapshot()) || sceneSeconds != pausedSeconds ||
                    progress.elapsed != pausedProgress.elapsed || progress.powerTime != pausedProgress.powerTime ||
                    progress.graceTime != pausedProgress.graceTime || progress.health != pausedProgress.health ||
                    progress.pearls != pausedProgress.pearls || messageTime != pausedMessageTime ||
                    camera.getX() != pausedCamera.x || camera.getY() != pausedCamera.y ||
                    body->velocityX != pausedVelocity.x || body->velocityY != pausedVelocity.y)
                    throw std::runtime_error("Paused world, timers, or camera changed");
            }
            return;
        }
        if (wasPaused && (net || peers))
        {
            // The shared scene may have advanced while this client's view was frozen.
            // Do not treat that gap as movement that should carry a platform rider.
            previousGround.clear(); previousFish.clear();
        }
        wasPaused = false;
        if (progress.finished()) worldTime.pause(); else worldTime.unpause();
        const float realDelta = options.verifyRun ? 1.f / 15 : options.verifyPause ? 1.f / 60
            : std::min(.05f, static_cast<float>(globalTime.getLastDelta()));
        if (!progress.finished()) sceneSeconds += realDelta;
        const float dt = playerTime.isPaused() || titleScreen ? 0.f : options.verifyRun ? realDelta
            : realDelta * static_cast<float>(playerTime.getScale());
        progress.tick(dt, playerTime.isPaused() || titleScreen ? 0.f : realDelta);
        messageTime = std::max(0.f, messageTime - dt);
        if (net && !progress.finished()) net->sync(objects, onSpawn);
        if (peers && !progress.finished()) peers->sync(objects, onSpawn);
        if (!net && !progress.finished())
        {
            const float delta = realDelta;
            if (peers)
            {
                const float shared = static_cast<float>((peerNowTic() - peers->getAnchor()) / 1'000'000.0);
                objects.forEach<MovementPath>([&](GameObject &, MovementPath &path)
                { if (path.initialized) path.elapsedTime = shared - delta; });
            }
            GameplaySystems::updateMovementPaths(objects, delta);
        }
        std::unordered_map<uint32_t, SDL_FPoint> groundDelta;
        objects.forEach<Transform, Sea::Ground>([&](GameObject &o, Transform &t, Sea::Ground &)
        {
            auto prev = previousGround.find(o.getId());
            groundDelta[o.getId()] = prev == previousGround.end() ? SDL_FPoint{0, 0}
                : SDL_FPoint{t.x - prev->second.x, t.y - prev->second.y};
            previousGround[o.getId()] = {t.x, t.y};
        });
        for (int i = 0; i < Sea::PearlCount; ++i)
            Sea::updatePearl(objects, *objects.find(pearlIds[i]), i, sceneSeconds);
        auto &t = *player.get<Transform>();
        auto &body = *player.get<PhysicsBody>();
        auto &sprite = *player.get<Sprite>();
        auto &input = *player.get<PlayerInput>();
        float axis = input.horizontalAxis();
        bool jump = input.jumpJustPressed(), swim = input.jumpPressed(), dive = input.downPressed();
        if (options.smokeTest && options.previewCheckpoint == 6) axis = .15f;
        if (options.verifyRun && !progress.finished())
        {
            // First verify protection using actual pickups and actual scene collisions.
            if (testPhase == 0)
            {
                auto *core = objects.find(powerIds[0]);
                t.x = core->get<Transform>()->x; t.y = core->get<Transform>()->y;
                axis = 0; swim = dive = jump = false; ++testPhase;
            }
            else if (testPhase == 1 || testPhase == 2)
            {
                GameObject *hazard = nullptr;
                objects.forEach<Sea::Hazard>([&](GameObject &o, Sea::Hazard &)
                { if (!hazard && o.has<Sea::Urchin>() == (testPhase == 2)) hazard = &o; });
                const auto &ht = *hazard->get<Transform>();
                t.x = ht.x - 24; t.y = ht.y - Sea::DiverHeight + 28;
                axis = 0; swim = dive = jump = false;
                if (testPhase == 1) verifiedFishShield = progress.powered() && Sea::touchesHazard(player, *hazard);
                else verifiedUrchinShield = progress.powered() && Sea::touchesHazard(player, *hazard);
                ++testPhase;
            }
            else if (testPhase == 3)
            {
                progress = {};
                respawn();
                ++testPhase;
                axis = 0; swim = dive = jump = false;
            }
            else if (testPhase == 4)
            {
                axis = 1; swim = dive = false;
                jump = motion.grounded && t.x > 385;
                if (t.y > Sea::SurfaceY + 30) ++testPhase;
            }
            else
            {
                int nextIndex = 0;
                while (nextIndex < Sea::PearlCount && progress.pearls[nextIndex]) ++nextIndex;
                if (routeTarget != nextIndex || progress.graceTime > 1.7f)
                {
                    leavingPearl = -1;
                    if (routeTarget >= 0 && nextIndex > routeTarget && progress.graceTime <= 1.7f)
                    {
                        const auto &last = *objects.find(pearlIds[routeTarget])->get<Transform>();
                        for (const auto &urchin : Sea::Urchins)
                            if (urchin.x() > last.x && urchin.x() < last.x + 250 && std::fabs(urchin.y() - last.y) < 100)
                                leavingPearl = routeTarget;
                    }
                    routeTarget = nextIndex; routeStage = 0; safeAscent = false;
                }
                float goalX = 1204;
                jump = swim = dive = false;
                if (leavingPearl >= 0)
                {
                    // Clear the quills before steering across the rest of this shelf.
                    const auto &last = *objects.find(pearlIds[leavingPearl])->get<Transform>();
                    goalX = last.x + last.width * .5f;
                    swim = true; jump = motion.grounded;
                    if (t.y + t.height < last.y - 8) leavingPearl = -1;
                }
                else if (nextIndex < Sea::PearlCount)
                {
                    const auto &next = *objects.find(pearlIds[nextIndex])->get<Transform>();
                    const float lowerApproach = next.y + 140;
                    if (routeStage == 0 && t.x + t.width * .5f > 1190) routeStage = 1;
                    if (routeStage == 1)
                    {
                        swim = t.y + body.velocityY * .25f > lowerApproach;
                        dive = t.y < lowerApproach - 15;
                        if (std::fabs(t.y - lowerApproach) < 25) routeStage = 2;
                    }
                    if (routeStage >= 2)
                    {
                        goalX = next.x + next.width * .5f;
                        swim = routeStage == 3 || t.y + body.velocityY * .25f > lowerApproach;
                        dive = false;
                        const bool attached = Sea::Pearls[nextIndex].movingIndex >= 0;
                        if (std::fabs(goalX - (t.x + t.width * .5f)) < (attached ? 30 : 12) &&
                            (attached || std::fabs(body.velocityX) < 40)) routeStage = 3;
                    }
                }
                else
                {
                    if (t.x + t.width * .5f > 1190) safeAscent = true;
                    swim = safeAscent;
                    if (safeAscent && t.y < Sea::SurfaceY + 160) goalX = (Sea::GateLeft + Sea::GateRight) * .5f;
                }
                const float diff = goalX - (t.x + t.width * .5f);
                const float wanted = std::clamp(diff * 3.f, -Sea::MoveSpeed, Sea::MoveSpeed);
                axis = wanted > body.velocityX + 30 ? 1.f : wanted < body.velocityX - 30 ? -1.f : 0.f;
                GameObject *support = objects.find(motion.ridingId);
                if (motion.grounded && support && support->has<Sea::Shore>())
                { testPhase = 4; axis = 1; jump = t.x > 385; swim = dive = false; }
            }
        }
        const SDL_FRect before = Sea::bounds(player);
        const int respawnsBeforeContact = respawnCount;
        if (!progress.finished())
        {
            // Inputs and the platform deltas are prepared on the main thread.
            // The shared worker owns motion/physics until wait() returns. Scene
            // mutation, networking, SDL rendering and damage run outside that phase.
            motionStep = [&]() { Sea::movePlayer(objects, player, motion, physics, dt, axis, jump,
                                                groundDelta, swim, dive, progress.powered()); };
            motionWorker.start();
            motionWorker.wait();
        }
        else body.velocityX = body.velocityY = 0;
        if (!progress.finished())
        {
            if (axis) sprite.flipHorizontal = axis < 0;
            sprite.frame = Sea::diverFrame(t, motion, progress.elapsed, axis != 0, progress.powered());
            sprite.color.a = progress.graceTime > 0 && static_cast<int>(progress.elapsed * 10) % 2 ? 130 : 255;
        }
        if (!progress.finished() && dt > 0)
        {
            for (int i = 0; i < Sea::PearlCount; ++i)
            {
                auto *o = objects.find(pearlIds[i]);
                if (Collision::checkCollision(player, *o))
                {
                    progress.pearls[i] = true; o->get<Sea::Pearl>()->collected = true; o->active = false;
                    say(progress.count() == Sea::PearlCount ? "ALL PEARLS SECURED! Swim UP to the surface and pass through the buoy opening." :
                        "Pearl secured. Keep diving - hold SPACE to swim back for any you miss.");
                }
            }
            for (auto id : powerIds)
            {
                auto *o = objects.find(id);
                if (Collision::checkCollision(player, *o))
                {
                    o->get<Sea::PowerUp>()->collected = true; o->active = false;
                    progress.activatePower();
                    say("3 SECOND OVERDRIVE! Use the yellow suit to cross the next hazard safely.", 3);
                }
            }
            bool touchedHazard = false;
            objects.forEach<Sea::Hazard>([&](GameObject &o, Sea::Hazard &)
            { if (Sea::touchesHazard(player, o)) touchedHazard = true; });
            if (touchedHazard && !progress.protectedNow() && progress.hit())
            {
                if (progress.gameOver) endDive();
                else { respawn(); say("Hit! Returned to your checkpoint. Recovered pearls are safe."); }
            }
            if (options.verifyRun && frames <= 3 &&
                (progress.health != Sea::MaxHealth || respawnCount != respawnsBeforeContact))
                throw std::runtime_error("Verification failed: powered contact caused damage or a respawn");
            for (int i = 1; i < Sea::SpawnCount; ++i)
                if (!progress.finished() && !progress.returning && i > progress.checkpoint && std::fabs(t.x - Sea::Spawns[i].x) < 180 &&
                    std::fabs(t.y - Sea::Spawns[i].y) < 110)
                {
                    progress.checkpoint = i;
                    say("Checkpoint secured. Recovered pearls stay safe; lives are not restored.");
                }
            pit.get<DeathZone>()->spawnPointName = Sea::Spawns[progress.checkpoint].name;
            const bool wasArmed = pit.get<DeathZone>()->armed;
            GameplaySystems::updateDeathZones(objects, playerId);
            if (wasArmed && !pit.get<DeathZone>()->armed)
            {
                // Falling out of the level always costs a life, including during overdrive.
                progress.loseLife();
                if (progress.gameOver) endDive(); else { respawn(); say("The abyss cost one life. Returned to your checkpoint."); }
            }
            if (Sea::updateMission(progress, Sea::bounds(player)))
                say("RECOVERY COMPLETE! Swim back UP through all 3 regions to the buoy above the sea.", 8);
            if (!progress.finished() && progress.returning)
            {
                const int next = t.y < Sea::SurfaceY - 100 ? 0 : t.y < Sea::Spawns[1].y - 50 ? 1 : 2;
                if (next < progress.checkpoint)
                {
                    progress.checkpoint = next;
                }
            }
            if (Sea::crossesExit(before, Sea::bounds(player), motion, progress))
            { progress.won = true; body.velocityX = body.velocityY = 0; }

        }

        std::vector<uint32_t> divers{playerId};
        objects.forEach<NetworkIdentity>([&](GameObject &o, NetworkIdentity &id)
        {
            if (id.isReplica && id.kind == Sea::KindDiver) divers.push_back(o.getId());
        });
        for (auto id : divers)
        {
            auto &actor = *objects.find(id);
            if (!diverSkins.count(id))
                diverSkins[id] = makeSprite("diver-appearance", 0, 0, Sea::DiverDrawWidth,
                                            Sea::DiverDrawHeight, suits, id == playerId ? 6 : 5, 8).getId();
            auto &skin = *objects.find(diverSkins[id]);
            const auto draw = Sea::diverDrawBounds(*actor.get<Transform>());
            auto &pose = *skin.get<Transform>();
            pose.x = draw.x; pose.y = draw.y; pose.width = draw.w; pose.height = draw.h;
            const auto &state = *actor.get<Sprite>();
            auto &appearance = *skin.get<Sprite>();
            appearance.texture = state.frame >= 8 ? swimmingSuits : suits;
            appearance.frame = state.frame % 8;
            appearance.flipHorizontal = state.flipHorizontal;
            appearance.color = state.color;
            skin.active = actor.active;
        }
        for (auto it = diverSkins.begin(); it != diverSkins.end(); )
        {
            if (!objects.find(it->first))
            { objects.destroy(it->second); it = diverSkins.erase(it); }
            else ++it;
        }
        const float seconds = sceneSeconds;
        objects.forEach<Transform, Sea::Hazard>([&](GameObject &o, Transform &ft, Sea::Hazard &)
        {
            if (o.has<Sea::Urchin>()) return;
            auto *s = o.get<Sprite>();
            s->frame = static_cast<int>(seconds * 8) % 4;
            auto prev = previousFish.find(o.getId());
            if (prev != previousFish.end() && ft.x != prev->second.x) s->flipHorizontal = ft.x < prev->second.x;
            previousFish[o.getId()] = {ft.x, ft.y};
        });
        for (int i = 0; i < Sea::PowerCount; ++i)
        {
            auto *pt = objects.find(powerIds[i])->get<Transform>();
            pt->y = Sea::Powers[i].y + std::sin(seconds * 3 + i) * 10;
        }
        for (std::size_t i = 0; i < bubbles.size(); ++i)
        {
            auto *o = objects.find(bubbles[i]);
            o->get<Transform>()->y = Sea::WorldHeight - std::fmod(i * 87.f + seconds * (20 + i % 4 * 8), Sea::WorldHeight - Sea::SurfaceY);
            o->get<Sprite>()->frame = static_cast<int>(seconds * 3 + i) % 3;
        }
        shield.active = progress.powered();
        auto *st = shield.get<Transform>();
        st->width = st->height = 142 + std::sin(seconds * 8) * 8;
        st->x = t.x + t.width * .5f - st->width * .5f; st->y = t.y + t.height * .5f - st->height * .5f;
        for (std::size_t i = 0; i < trail.size(); ++i)
        {
            auto *o = objects.find(trail[i]); o->active = progress.powered();
            auto *p = o->get<Transform>();
            p->x = t.x + t.width * .5f + std::cos(seconds * 3 + i * .8f) * 65;
            p->y = t.y + t.height * .5f + std::sin(seconds * 3 + i * .8f) * 55;
            auto *bubble = objects.find(swimTrail[i]);
            const float phase = std::fmod(seconds * 1.6f + i * .125f, 1.f);
            if (!progress.finished())
                bubble->active = Sea::isSwimming(t, motion) && (swim || dive || std::fabs(body.velocityX) > 20);
            auto *bt = bubble->get<Transform>();
            bt->x = t.x + t.width * .5f - body.velocityX * phase * .22f + std::sin(i + seconds * 2) * 8;
            bt->y = t.y + t.height * .6f + body.velocityY * phase * -.2f - phase * 35;
            bubble->get<Sprite>()->frame = phase < .65f ? 0 : phase < .85f ? 1 : 2;
            bubble->get<Sprite>()->color.a = static_cast<Uint8>((1 - phase) * 170);
        }
        buoy.get<Transform>()->y = Sea::BuoyY + std::sin(seconds * 1.8f) * 2;
        gateGlow.get<Sprite>()->color = progress.returning ? SDL_Color{255, 226, 135, 220} : SDL_Color{93, 154, 180, 85};
        // Vertical camera with a clear view below the captain, clamped at both ends.
        if (!progress.finished()) camera.setPosition(t.x + t.width * .5f - camera.getViewportWidth() * .5f,
                                                    t.y - camera.getViewportHeight() * (420.f / 720));
        SDL_SetRenderTarget(renderer, waveArt);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        for (int x = 0; x < 1280; x += 8)
            SeaVisuals::rect(renderer, static_cast<float>(x), 7 + std::sin(x * .035f + seconds * 2) * 3,
                             9, 3, {160, 230, 235, 205});
        SDL_SetRenderTarget(renderer, nullptr);
        hud.get<Transform>()->x = camera.getX(); hud.get<Transform>()->y = camera.getY();
        if (messageTime == 0)
            message = progress.returning ? "RETURN TO SURFACE: hold SPACE to swim up, then pass UP through the buoy hole." :
                "Explore the three regions. Space swims upward; S dives. Golden cores protect you for 3 seconds.";
        SeaVisuals::hud(renderer, hudTexture, progress, t.y, seconds, message, playerTime.isPaused(), pearlArt, bubbleArt,
                        titleArt, titleScreen);
        if (net) net->publish(objects);
        if (peers) peers->publish(objects);
        if (options.verifyFoundations && frames == 12) capture(".pixel.bmp");
        if (options.verifyFoundations && frames == 17) capture(".proportional.bmp");
        if (options.verifyPause && (frames == 80 || frames == 160))
        {
            const float elapsed = sceneSeconds - pausedSeconds;
            if (sameScene(pausedScene, sceneSnapshot()) || elapsed <= 0 || elapsed > .02f || worldTime.isPaused())
                throw std::runtime_error("World did not resume normally after pause");
        }
        if (options.verifyPause && frames == 180)
        {
            capture();
            std::cout << "PASS: two pause/resume cycles freeze the entire scene, camera, lives, and power/game timers.\n";
            quit();
        }
        const double testSeconds = (SDL_GetTicksNS() - testStarted) / 1'000'000'000.0;
        if (options.smokeTest && !smokeReported && (net || peers ? testSeconds >= 3 : frames == 12))
        {
            smokeReported = true;
            capture();
            int divers = 0, movers = 0, swimmers = 0, walkers = 0;
            objects.forEach<NetworkIdentity, Sprite>([&](GameObject &, NetworkIdentity &id, Sprite &s)
            {
                if (id.isReplica && id.kind == Sea::KindDiver)
                { ++divers; s.frame >= 8 ? ++swimmers : ++walkers; }
                else if (id.isReplica && id.ownerId == 0) ++movers;
            });
            if (net && movers != Sea::MoverCount) throw std::runtime_error("Missing replicated scene movers");
            if (diverSkins.size() != static_cast<std::size_t>(divers + 1))
                throw std::runtime_error("Missing visible diver appearances");
            std::cout << "Smoke test: " << assetCount << " art assets loaded; remote divers=" << divers << "; remote movers=" << movers
                      << "; scene movers=" << Sea::MoverCount << "; remote swimmers=" << swimmers
                      << "; remote walkers=" << walkers << "; diver appearances=" << diverSkins.size()
                      << "; motion worker frames=" << motionWorker.getFramesBuilt()
                      << "; messages/s=" << (net ? (net->getMessagesSent() - initialMessages) / testSeconds : 0) << std::endl;
            if (!net && !peers) quit();
        }

        if (options.smokeTest && (net || peers) && testSeconds >= 4.5) quit();
        if (options.verifyFoundations && frames == 90)
        {
            if (motionThread == mainThread || motionWorker.getFramesBuilt() < 85 ||
                objects.getScaleMode() != ScaleMode::Percentage || !motion.grounded ||
                physics.getGravity() != Sea::Gravity)
                throw std::runtime_error("Foundation integration checks failed");
            capture();
            std::cout << "PASS: shared configurable physics, motion worker, pixel/proportional toggles, resize, and time scales.\n";
            quit();
        }
        if (options.verifyRun && progress.won && !verificationFinished)
        {
            verificationFinished = true;
            if (!verifiedFishShield || !verifiedUrchinShield) throw std::runtime_error("Immunity checks incomplete");
            capture();
            std::cout << "PASS: power immunity -> 3 regions / " << Sea::PearlCount
                      << " pearls including the circular shelf -> reach the bottom -> swim to surface -> buoy opening / frozen victory.\n";
            quit();
        }
        if (options.verifyRun && frames > 7200)
            throw std::runtime_error("Verification route timed out at x=" + std::to_string(t.x) + " y=" + std::to_string(t.y) +
                                     " pearls=" + std::to_string(progress.count()));
        if (options.verifyRun && progress.gameOver)
            throw std::runtime_error("Verification exhausted all lives at pearl " + std::to_string(progress.count()));
        if (options.verifyRun && lastVerifiedPearls != progress.count())
        {
            lastVerifiedPearls = progress.count();
            std::cout << "Route: " << lastVerifiedPearls << "/" << Sea::PearlCount << " pearls; lives=" << progress.health << std::endl;
        }
        if (options.verifyRun && frames % 120 == 0)
            std::cout << "Route frame " << frames << " phase " << testPhase << " at " << t.x << "," << t.y
                      << " velocity " << body.velocityX << "," << body.velocityY << std::endl; });
    if (net)
        net->disconnect();
    for (auto *texture : textures)
        SDL_DestroyTexture(texture);
    return 0;
}

int main(int argc, char *argv[])
{
    try
    {
        return runGame(argc, argv);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Abyss Run: " << e.what() << '\n';
        return 1;
    }
}
