#include "Entity.h"

Entity::Entity(float x, float y, float w, float h, SDL_Color color)
    : x(x), y(y), width(w), height(h), color(color) {}

SDL_FRect Entity::getBounds() const {
    return SDL_FRect{ x, y, width, height };
}
