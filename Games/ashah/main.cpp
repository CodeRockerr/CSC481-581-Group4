#include "Engine.h"
#include "Collision.h"
#include "Image.h"
#include <cmath>
#include <stdexcept>
#include <string>

// Gemini/ChatGPT "magenta" is never exact #FF00FF (measured ~248,4,248
// with lots of nearby pinks). Walk the pixels and punch those out to
// alpha=0 so SDL_RenderTexture can blend them away.
static bool isChromaMagenta(Uint8 r, Uint8 g, Uint8 b)
{
    return r >= 160 && b >= 160 && g <= 90 && (r - g) > 70 && (b - g) > 70;
}

static void applyMagentaKey(SDL_Surface *surface)
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

static SDL_Texture *loadTexture(SDL_Renderer *renderer, const char *path, bool chromaKey)
{
    SDL_Surface *surface = loadImage(path);
    if (!surface)
    {
        throw std::runtime_error(std::string("Could not load ") + path + ": " + SDL_GetError());
    }

    if (chromaKey)
        applyMagentaKey(surface);

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

// Sprite sheets include magenta padding, so the draw rect is much larger
// than the visible body. Copy the entity with insets and let the engine
// AABB test those tighter boxes.
static Entity bodyHitbox(const Entity &e, float padL, float padR, float padT, float padB)
{
    Entity box = e;
    box.x = e.x + e.width * padL;
    box.y = e.y + e.height * padT;
    box.width = e.width * (1.0f - padL - padR);
    box.height = e.height * (1.0f - padT - padB);
    return box;
}

int main(int argc, char *agv[])
{
    Engine engine("Lost Under the Sea");
    EntityManager &entities = engine.getEntities();
    SDL_Renderer *renderer = engine.getRenderer().getHandle();

    engine.getPhysics().setGravity(1600.0f);

    // SDL may not actually open at 1920x1080 on a Mac laptop. Read the
    // real window size and use THAT as layout + Task 6 reference, so the
    // shelf/diver/fish sit inside the visible ocean instead of the blue void.
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(engine.getWindow().getHandle(), &width, &height);
    engine.getWindow().setSize(width, height);
    entities.setReferenceResolution(width, height);
    const int referenceWidth = width;
    const int referenceHeight = height;

    SDL_Texture *backgroundTex = loadTexture(renderer, "Games/ashah/assets/background.png", false);
    SDL_Texture *shelfTex = loadTexture(renderer, "Games/ashah/assets/shelf.png", true);
    SDL_Texture *diverTex = loadTexture(renderer, "Games/ashah/assets/diver.png", true);
    SDL_Texture *fishTex = loadTexture(renderer, "Games/ashah/assets/fish.png", true);
    SDL_Texture *bubbleTex = loadTexture(renderer, "Games/ashah/assets/bubble.png", true);

    Entity *background = entities.createEntity(0.0f, 0.0f, float(width), float(height));
    entities.setTexture(background, backgroundTex, 1);
    background->affectedByGravity = false;

    // Leave water on both ends so walking off the ledge is obvious.
    const float shelfPadTop = 0.20f;
    const float diverPadBottom = 0.10f;

    const float shelfWidth = width * 0.70f;
    const float shelfHeight = height * 0.14f;
    const float shelfX = width * 0.15f;
    const float shelfY = height * 0.72f;
    Entity *shelf = entities.createEntity(shelfX, shelfY, shelfWidth, shelfHeight);
    entities.setTexture(shelf, shelfTex, 1);
    shelf->affectedByGravity = false;

    const float walkLeft = shelfX + shelfWidth * 0.08f;
    const float walkRight = shelfX + shelfWidth * 0.92f;

    // Size from one animation frame, then widen slightly so the profile
    // sprites are not stretched into a tall skinny box.
    float diverTexW = 0.0f;
    float diverTexH = 0.0f;
    SDL_GetTextureSize(diverTex, &diverTexW, &diverTexH);
    const float diverFrameAspect = (diverTexW / 4.0f) / diverTexH;
    const float diverH = height * 0.13f;
    const float diverW = diverH * diverFrameAspect * 1.20f;

    const float spawnX = width * 0.40f;
    const float shelfWalkY = shelfY + shelfHeight * shelfPadTop;
    const float standY = shelfWalkY - diverH * (1.0f - diverPadBottom);
    Entity *diver = entities.createEntity(spawnX, standY, diverW, diverH);
    entities.setTexture(diver, diverTex, 4);
    diver->affectedByGravity = true;
    bool grounded = true;
    float diverAnim = 0.0f;

    float fishTexW = 0.0f;
    float fishTexH = 0.0f;
    SDL_GetTextureSize(fishTex, &fishTexW, &fishTexH);
    const float fishFrameAspect = (fishTexW / 4.0f) / fishTexH;
    const float fishH = height * 0.10f;
    const float fishW = fishH * fishFrameAspect * 1.25f;
    const float fishY = standY + diverH * 0.45f - fishH * 0.50f;
    Entity *fish = entities.createEntity(shelfX + shelfWidth * 0.55f, fishY, fishW, fishH);
    entities.setTexture(fish, fishTex, 4);
    fish->affectedByGravity = false;
    float fishMinX = walkLeft;
    float fishMaxX = walkRight - fishW;
    float fishSpeed = 140.0f;
    int fishDir = 1;
    float fishAnim = 0.0f;

    auto dropInFromTop = [&]()
    {
        diver->x = spawnX;
        diver->y = -diver->height;
        diver->velocityX = 0.0f;
        diver->velocityY = 80.0f;
        grounded = false;
    };

    const int bubbleCount = 5;
    Entity *bubbles[5];
    float bubblePhase[5];
    float bubblePop[5];
    for (int i = 0; i < bubbleCount; i++)
    {
        float size = 18.0f + i * 6.0f;
        bubbles[i] = entities.createEntity(
            width * (0.08f + i * 0.16f),
            height * (0.20f + i * 0.12f),
            size,
            size);
        entities.setTexture(bubbles[i], bubbleTex, 3);
        bubbles[i]->affectedByGravity = false;
        bubbles[i]->velocityY = -45.0f - i * 12.0f;
        bubblePhase[i] = i * 0.8f;
        bubblePop[i] = -1.0f;
    }

    engine.run([&](float deltaTime)
               {
        if (Input::isKeyJustPressed(SDL_SCANCODE_TAB) ||
            Input::isKeyJustPressed(SDL_SCANCODE_T))
        {
            entities.toggleScaleMode();
            if (entities.getScaleMode() == ScaleMode::Percentage)
            {
                engine.getWindow().resizeWindow(
                    static_cast<int>(referenceWidth * 0.65f),
                    static_cast<int>(referenceHeight * 0.65f));
            }
            else
            {
                engine.getWindow().resizeWindow(referenceWidth, referenceHeight);
            }
            const char *modeName =
                entities.getScaleMode() == ScaleMode::Pixel ? "Pixel" : "Percentage";
            std::string title = std::string("Lost Under the Sea — ") + modeName;
            SDL_SetWindowTitle(engine.getWindow().getHandle(), title.c_str());
            SDL_Log("Scale mode: %s", modeName);
        }

        const int currentWidth = engine.getWindow().getWidth();
        const int currentHeight = engine.getWindow().getHeight();

        SDL_SetRenderLogicalPresentation(
            renderer,
            currentWidth,
            currentHeight,
            SDL_LOGICAL_PRESENTATION_STRETCH);

        const float scaleX =
            (entities.getScaleMode() == ScaleMode::Percentage)
                ? static_cast<float>(currentWidth) / static_cast<float>(referenceWidth)
                : 1.0f;
        const float scaleY =
            (entities.getScaleMode() == ScaleMode::Percentage)
                ? static_cast<float>(currentHeight) / static_cast<float>(referenceHeight)
                : 1.0f;
        background->width = currentWidth / scaleX;
        background->height = currentHeight / scaleY;
        const float moveSpeed = 380.0f;
        bool moving = false;
        diver->velocityX = 0.0f;

        if (Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT))
        {
            diver->velocityX = -moveSpeed;
            moving = true;
        }
        if (Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT))
        {
            diver->velocityX = moveSpeed;
            moving = true;
        }
        if ((Input::isKeyJustPressed(SDL_SCANCODE_W) ||
             Input::isKeyJustPressed(SDL_SCANCODE_UP) ||
             Input::isKeyJustPressed(SDL_SCANCODE_SPACE)) &&
            grounded)
        {
            diver->velocityY = -720.0f;
            grounded = false;
        }

        if (moving && grounded)
        {
            diverAnim += deltaTime;
            diver->spriteFrame = 1 + (static_cast<int>(diverAnim * 8.0f) % 3);
        }
        else if (!grounded)
        {
            diver->spriteFrame = 1; // mid-stride as jump pose
        }
        else
        {
            diverAnim = 0.0f;
            diver->spriteFrame = 0;
        }
        fish->y = fishY;
        fish->x += fishSpeed * fishDir * deltaTime;
        if (fish->x >= fishMaxX)
        {
            fish->x = fishMaxX;
            fishDir = -1;
        }
        else if (fish->x <= fishMinX)
        {
            fish->x = fishMinX;
            fishDir = 1;
        }
        fishAnim += deltaTime;
        fish->spriteFrame = static_cast<int>(fishAnim * 8.0f) % 4;

        for (int i = 0; i < bubbleCount; ++i)
        {
            bubblePhase[i] += deltaTime;
            bubbles[i]->x += std::sin(bubblePhase[i] * 2.0f) * 18.0f * deltaTime;
            if (bubblePop[i] < 0.0f)
            {
                bubbles[i]->spriteFrame = 0;
                if (bubbles[i]->y <= referenceHeight * 0.10f)
                {
                    bubblePop[i] = 0.0f;
                    bubbles[i]->velocityY = 0.0f;
                }
            }
            else
            {
                bubblePop[i] += deltaTime;
                bubbles[i]->spriteFrame = (bubblePop[i] < 0.12f) ? 1 : 2;
                if (bubblePop[i] > 0.28f)
                {
                    bubbles[i]->x = (i + 1) * (referenceWidth / 6.0f);
                    bubbles[i]->y = static_cast<float>(referenceHeight);
                    bubbles[i]->velocityY = -45.0f - i * 12.0f;
                    bubblePop[i] = -1.0f;
                }
            }
        }

        const float shelfWalkY = shelf->y + shelf->height * shelfPadTop;
        const float diverFeet = diver->y + diver->height * (1.0f - diverPadBottom);
        const float diverMidX = diver->x + diver->width * 0.5f;
        const bool overShelf = diverMidX > walkLeft && diverMidX < walkRight;
        const bool onShelf = Collision::checkCollision(*diver, *shelf) && overShelf;

        if (onShelf && diver->velocityY >= 0.0f && diverFeet >= shelfWalkY - 16.0f)
        {
            diver->y = shelfWalkY - diver->height * (1.0f - diverPadBottom);
            diver->velocityY = 0.0f;
            grounded = true;
        }
        else
        {
            grounded = false;
        }

        const Entity diverHit = bodyHitbox(*diver, 0.30f, 0.22f, 0.10f, 0.12f);
        const Entity fishHit = bodyHitbox(*fish, 0.20f, 0.18f, 0.42f, 0.22f);
        if (Collision::checkCollision(diverHit, fishHit))
        {
            SDL_Log("The fish caught the diver!");
            dropInFromTop();
        }

        if (diver->y > static_cast<float>(referenceHeight))
        {
            dropInFromTop();
        } });

    SDL_DestroyTexture(backgroundTex);
    SDL_DestroyTexture(shelfTex);
    SDL_DestroyTexture(diverTex);
    SDL_DestroyTexture(fishTex);
    SDL_DestroyTexture(bubbleTex);
    return 0;
}
