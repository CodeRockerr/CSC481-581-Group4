#pragma once
#include "SeaGameplay.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <stdexcept>
#include <vector>

namespace SeaVisuals
{
    inline void rect(SDL_Renderer *r, float x, float y, float w, float h, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_FRect box{x, y, w, h};
        SDL_RenderFillRect(r, &box);
    }
    inline void triangle(SDL_Renderer *r, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_Color color)
    {
        SDL_FColor f{color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f};
        SDL_Vertex v[] = {{a, f, {}}, {b, f, {}}, {c, f, {}}};
        SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
    }
    inline void circle(SDL_Renderer *r, float x, float y, float radius, SDL_Color color)
    {
        for (int i = 0; i < 32; ++i)
        {
            const float a = i * 6.2831853f / 32, b = (i + 1) * 6.2831853f / 32;
            triangle(r, {x, y}, {x + std::cos(a) * radius, y + std::sin(a) * radius},
                     {x + std::cos(b) * radius, y + std::sin(b) * radius}, color);
        }
    }
    inline void text(SDL_Renderer *r, float x, float y, float scale, SDL_Color c, const std::string &s)
    {
        SDL_SetRenderScale(r, scale, scale);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDebugText(r, x / scale, y / scale, s.c_str());
        SDL_SetRenderScale(r, 1, 1);
    }
    inline void centered(SDL_Renderer *r, float y, float scale, SDL_Color c, const std::string &s)
    {
        text(r, (Sea::ViewWidth - s.size() * 8 * scale) * 0.5f, y, scale, c, s);
    }
    inline SDL_Texture *target(SDL_Renderer *r, int w, int h)
    {
        SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        if (!t)
            throw std::runtime_error(SDL_GetError());
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
        SDL_SetRenderTarget(r, t);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        return t;
    }
    inline SDL_Texture *glow(SDL_Renderer *r)
    {
        auto *t = target(r, 96, 96);
        for (int radius = 46; radius > 0; radius -= 3)
            circle(r, 48, 48, static_cast<float>(radius), {255, 255, 255, static_cast<Uint8>(radius < 16 ? 65 : 10)});
        SDL_SetRenderTarget(r, nullptr);
        return t;
    }
    inline SDL_Texture *power(SDL_Renderer *r)
    {
        auto *t = target(r, 80, 80);
        circle(r, 40, 40, 35, {255, 176, 47, 30});
        circle(r, 40, 40, 25, {255, 189, 67, 110});
        circle(r, 40, 40, 19, {255, 231, 122, 255});
        circle(r, 40, 40, 15, {25, 91, 109, 255});
        triangle(r, {43, 19}, {29, 43}, {44, 40}, {255, 245, 182, 255});
        triangle(r, {36, 60}, {51, 34}, {36, 38}, {255, 245, 182, 255});
        SDL_SetRenderTarget(r, nullptr);
        return t;
    }
    inline SDL_Texture *urchins(SDL_Renderer *r, int width)
    {
        auto *t = target(r, width, static_cast<int>(Sea::UrchinHeight));
        const int count = Sea::urchinCount(static_cast<float>(width));
        const float step = static_cast<float>(width) / count;
        const float radius = Sea::urchinRadius(static_cast<float>(width));
        for (int i = 0; i < count; ++i)
        {
            const float x = (i + .5f) * step;
            circle(r, x, 33, radius, {31, 65, 76, 255});
            for (int spine = 0; spine < 12; ++spine)
            {
                const float a = spine * 6.2831853f / 12;
                triangle(r, {x + std::cos(a - .2f) * 7, 21 + std::sin(a - .2f) * 7},
                         {x + std::cos(a) * radius, 21 + std::sin(a) * radius},
                         {x + std::cos(a + .2f) * 7, 21 + std::sin(a + .2f) * 7},
                         spine % 2 ? SDL_Color{255, 247, 159, 255} : SDL_Color{255, 128, 226, 255});
            }
            circle(r, x, 21, 8, {166, 32, 175, 255});
            circle(r, x - 2, 18, 5, {255, 103, 219, 255});
            rect(r, x - 4, 16, 3, 3, {255, 255, 197, 255});
        }
        SDL_SetRenderTarget(r, nullptr);
        return t;
    }
    inline SDL_Texture *shore(SDL_Renderer *r)
    {
        auto *t = target(r, 1280, 720);
        for (int y = 0; y < 720; y += 4)
        {
            const bool water = y >= Sea::SurfaceY;
            rect(r, 0, static_cast<float>(y), 1280, 4, water ? SDL_Color{22, 117, 156, 255} : SDL_Color{static_cast<Uint8>(142 + y * .08f), static_cast<Uint8>(195 + y * .05f), 222, 255});
        }
        circle(r, 1010, 170, 56, {255, 241, 191, 255});
        for (int i = 0; i < 6; ++i)
        {
            const float x = i * 260.f - 40;
            circle(r, x, 270 + i % 2 * 30.f, 35, {227, 240, 239, 255});
            circle(r, x + 40, 265 + i % 2 * 30.f, 47, {227, 240, 239, 255});
        }
        triangle(r, {0, 390}, {90, 350}, {280, 520}, {82, 129, 119, 255});
        rect(r, 0, 460, 130, 260, {68, 91, 90, 255});
        rect(r, 0, 446, 140, 18, {93, 153, 114, 255});
        SDL_SetRenderTarget(r, nullptr);
        return t;
    }
    inline SDL_Texture *blendRegion(SDL_Renderer *r, SDL_Texture *art, int height, bool fadeTop)
    {
        auto *t = target(r, 1280, height);
        // Drawing into transparent targets stores premultiplied color. Preserve
        // that color when compositing the fade over the previous background.
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        float w, h;
        SDL_GetTextureSize(art, &w, &h);
        // Skip the bright surface painted into the top of each supplied region.
        // A 160px overlap fades into the preceding region on both trips.
        for (int y = 0; y < height; y += 4)
        {
            const float strip = static_cast<float>(std::min(4, height - y));
            SDL_FRect src{0, h * (.14f + .86f * y / height), w, h * .86f * strip / height};
            SDL_FRect dst{0, static_cast<float>(y), 1280, strip};
            SDL_SetTextureAlphaMod(art, fadeTop ? static_cast<Uint8>(255 * std::min(1.f, y / 160.f)) : 255);
            SDL_RenderTexture(r, art, &src, &dst);
        }
        SDL_SetTextureAlphaMod(art, 255);
        SDL_SetRenderTarget(r, nullptr);
        return t;
    }
    inline SDL_Texture *regionBackdrop(SDL_Renderer *r, SDL_Texture *base, int region)
    {
        auto *t = target(r, 1280, 1440);
        float w, h;
        SDL_GetTextureSize(base, &w, &h);
        const SDL_Color tint[] = {{126, 220, 202, 255}, {180, 153, 130, 255}, {112, 108, 185, 255}};
        SDL_SetTextureColorMod(base, tint[region].r, tint[region].g, tint[region].b);
        SDL_FRect src{0, h * .25f, w, h * .75f}, top{0, 0, 1280, 720}, bottom{0, 720, 1280, 720};
        SDL_RenderTextureRotated(r, base, &src, &top, 0, nullptr, SDL_FLIP_VERTICAL);
        SDL_RenderTexture(r, base, &src, &bottom);
        SDL_SetTextureColorMod(base, 255, 255, 255);
        for (int side = 0; side < 2; ++side)
            for (int i = 0; i < 9; ++i)
            {
                const float x = side ? 1230.f : 50.f, y = i * 170.f + 60;
                if (region == 0)
                {
                    rect(r, x, y, 5, 100, {29, 106, 104, 210});
                    triangle(r, {x, y + 60}, {x + (side ? -80 : 80), y + 5}, {x, y + 5}, {49, 140, 126, 190});
                    circle(r, x + (side ? -20 : 20), y + 95, 18, {154, 83, 126, 120});
                }
                else if (region == 1)
                {
                    const float left = side ? 1120.f : 0;
                    rect(r, left, y, 160, 42, {55, 59, 65, 220});
                    rect(r, x, y - 80, 10, 160, {72, 73, 70, 220});
                    triangle(r, {x, y}, {x + (side ? -120 : 120), y - 85}, {x, y - 60}, {70, 77, 76, 210});
                }
                else
                {
                    triangle(r, {side ? 1280.f : 0, y - 100}, {x + (side ? -55 : 55), y},
                             {side ? 1280.f : 0, y + 100}, {20, 28, 62, 240});
                    circle(r, x + (side ? -20 : 20), y, 5, {130, 128, 222, 180});
                }
            }
        SDL_SetRenderTarget(r, nullptr);
        return t;
    }
    inline void victory(SDL_Renderer *r, const Sea::Progress &progress)
    {
        rect(r, 220, 148, 840, 185, {4, 27, 43, 228});
        rect(r, 240, 165, 800, 2, {255, 219, 137, 255});
        centered(r, 192, 3, {255, 232, 168, 255}, "WELL DONE, CAPTAIN!");
        centered(r, 238, 2, {221, 248, 241, 255}, "All pearls secured safely.");
        char stats[100];
        std::snprintf(stats, sizeof(stats), "%d PEARLS / %.0fs / R TO DIVE AGAIN", progress.count(), progress.elapsed);
        centered(r, 285, 1.5f, {145, 212, 218, 255}, stats);
    }
    inline void hud(SDL_Renderer *r, SDL_Texture *canvas, const Sea::Progress &progress,
                    float depth, float time, const std::string &message, bool paused,
                    SDL_Texture *pearl, SDL_Texture *bubble, SDL_Texture *logo, bool titleScreen = false)
    {
        SDL_SetRenderTarget(r, canvas);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        if (titleScreen)
        {
            rect(r, 0, 0, 1280, 720, {3, 23, 38, 165});
            if (logo)
            {
                float w, h;
                SDL_GetTextureSize(logo, &w, &h);
                SDL_FRect dst{230, 108, 820, 820 * h / w};
                SDL_RenderTexture(r, logo, nullptr, &dst);
            }
            else
                centered(r, 200, 4, {255, 224, 148, 255}, "LOST UNDER THE SEA");
            centered(r, 407, 2, {133, 238, 249, 255}, "A B Y S S   R U N");
            centered(r, 465, 2.5f, {255, 234, 168, 255}, "PRESS ENTER OR SPACE TO DIVE");
            centered(r, 525, 1.5f, {221, 245, 248, 255}, "Recover " + std::to_string(Sea::PearlCount) + " pearls across 3 regions. Return through the surface buoy.");
            centered(r, 562, 1.5f, {172, 221, 236, 255}, "A/D STEER   SPACE SWIM / JUMP   S DIVE   P PAUSE   F SCALE   1/2/3 TIME");
            SDL_SetRenderTarget(r, nullptr);
            return;
        }
        if (progress.won)
        {
            victory(r, progress);
            SDL_SetRenderTarget(r, nullptr);
            return;
        }
        if (progress.gameOver)
        {
            rect(r, 220, 148, 840, 185, {4, 27, 43, 228});
            centered(r, 192, 3, {255, 158, 127, 255}, "DIVE ENDED");
            centered(r, 238, 2, {221, 248, 241, 255}, "No air remaining.");
            centered(r, 285, 1.5f, {145, 212, 218, 255}, "R TO START A NEW DIVE  /  ESC TO EXIT");
            SDL_SetRenderTarget(r, nullptr);
            return;
        }
        if (depth > Sea::SurfaceY)
            rect(r, 0, 115, 1280, 510, {17, 118, 162, 12});
        rect(r, 20, 16, 1240, 98, {3, 20, 36, 235});
        rect(r, 20, 16, 4, 98, {83, 217, 217, 255});
        if (logo)
        {
            float w, h;
            SDL_GetTextureSize(logo, &w, &h);
            SDL_FRect dst{40, 20, 70 * w / h, 70};
            SDL_RenderTexture(r, logo, nullptr, &dst);
        }
        else
            text(r, 40, 32, 2, {221, 250, 246, 255}, "LOST UNDER THE SEA");
        text(r, 40, 94, 1.5f, {116, 213, 214, 255}, progress.returning ? "RETURN TO SURFACE" : "DESCEND / RECOVER");
        char line[128];
        std::snprintf(line, sizeof(line), "DEPTH %04.0fm", std::max(0.f, depth - Sea::SurfaceY) * .1f);
        text(r, 1020, 34, 2, {161, 221, 238, 255}, line);
        const char *region = depth < Sea::SurfaceY ? "SHORE / SURFACE" : depth < Sea::RegionStarts[1] ? "1 CORAL GARDENS"
                                                                     : depth < Sea::RegionStarts[2]   ? "2 THE SHIPWRECK"
                                                                                                      : "3 THE ABYSS";
        text(r, 1010, 69, 1.5f, {118, 164, 190, 255}, region);
        for (int i = 0; i < Sea::PearlCount; ++i)
        {
            SDL_FRect dst{410 + i * 39.f, 40, 27, 27};
            SDL_SetTextureAlphaMod(pearl, progress.pearls[i] ? 255 : 45);
            SDL_RenderTexture(r, pearl, nullptr, &dst);
        }
        SDL_SetTextureAlphaMod(pearl, 255);
        std::snprintf(line, sizeof(line), "PEARLS %d / %d", progress.count(), Sea::PearlCount);
        text(r, 450, 79, 1.5f, {255, 224, 163, 255}, line);
        float bw, bh;
        SDL_GetTextureSize(bubble, &bw, &bh);
        SDL_FRect src{0, 0, bw / 3, bh};
        for (int i = 0; i < Sea::MaxHealth; ++i)
        {
            SDL_FRect dst{800 + i * 32.f, 36, 24, 24};
            SDL_SetTextureAlphaMod(bubble, i < progress.health ? 255 : 35);
            SDL_RenderTexture(r, bubble, &src, &dst);
        }
        SDL_SetTextureAlphaMod(bubble, 255);
        text(r, 810, 77, 1.5f, {151, 215, 231, 255}, "AIR / LIVES");
        if (progress.powered())
        {
            rect(r, 370, 130, 540, 46, {26, 49, 57, 235});
            rect(r, 378, 166, 524 * progress.powerTime / Sea::PowerDuration, 4, {255, 216, 99, 255});
            std::snprintf(line, sizeof(line), "OVERDRIVE  %.1fs  /  INVINCIBLE + FAST", progress.powerTime);
            text(r, 388, 141, 1.5f, {255, 230, 154, 255}, line);
        }
        if (paused)
        {
            rect(r, 430, 300, 420, 90, {3, 20, 36, 240});
            centered(r, 324, 3, {242, 244, 208, 255}, "GAME PAUSED");
            centered(r, 365, 1.5f, {155, 219, 226, 255}, "P TO CONTINUE");
        }
        rect(r, 20, 625, 1240, 79, {3, 20, 36, 232});
        text(r, 40, 641, 1.5f, {255, 226, 162, 255}, message);
        text(r, 40, 674, 1.5f, {161, 206, 217, 255}, "A/D STEER   SPACE SWIM / JUMP   S DIVE / DROP   P PAUSE   F SCALE   1/2/3 TIME   R RESTART   ESC EXIT");
        // A small depth rail shows where the remaining pearls are.
        rect(r, 1254, 160, 3, 430, {61, 120, 149, 200});
        for (int i = 0; i < Sea::PearlCount; ++i)
            circle(r, 1255, 160 + Sea::Pearls[i].y / Sea::WorldHeight * 430, 4,
                   progress.pearls[i] ? SDL_Color{88, 187, 181, 255} : SDL_Color{255, 220, 146, 255});
        circle(r, 1255, 160 + std::clamp(depth / Sea::WorldHeight, 0.f, 1.f) * 430, 6, {215, 253, 251, 255});
        SDL_SetRenderTarget(r, nullptr);
    }
}
