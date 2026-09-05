#include "Image.h"

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cassert>

#define STBI_ONLY_PNG
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

SDL_Surface *loadImage(const char *path)
{
    size_t fileSize = 0;
    void *fileData = SDL_LoadFile(path, &fileSize);
    if (!fileData)
    {
        return nullptr;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *pixels = stbi_load_from_memory(
        static_cast<const unsigned char *>(fileData),
        static_cast<int>(fileSize),
        &width,
        &height,
        &channels,
        STBI_rgb_alpha);
    SDL_free(fileData);

    if (!pixels)
    {
        SDL_SetError("Could not decode image");
        return nullptr;
    }

    SDL_Surface *surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (!surface)
    {
        stbi_image_free(pixels);
        return nullptr;
    }

    if (!SDL_LockSurface(surface))
    {
        SDL_DestroySurface(surface);
        stbi_image_free(pixels);
        return nullptr;
    }

    for (int row = 0; row < height; ++row)
    {
        std::memcpy(static_cast<unsigned char *>(surface->pixels) + row * surface->pitch,
                    pixels + row * width * 4,
                    static_cast<size_t>(width) * 4);
    }

    SDL_UnlockSurface(surface);
    stbi_image_free(pixels);
    return surface;
}
