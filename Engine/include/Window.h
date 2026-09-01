#pragma once
#include <SDL3/SDL.h>
#include <string>

// Task 1 (part 1): Core Graphics Setup — window creation.
// Pass width=0, height=0 (the default) to auto-size the window to 80% of
// the user's actual screen, so the game adapts to whatever machine it
// runs on instead of assuming a fixed resolution like 1920x1080.
class Window
{
public:
    Window(const std::string &title, int width = 0, int height = 0);
    ~Window();

    SDL_Window *getHandle() const { return window; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }

    // Called by Engine when SDL reports the window was resized, so
    // everything downstream (entity scaling, fall-off checks) uses the
    // current size, not the size at startup.
    void setSize(int w, int h)
    {
        width = w;
        height = h;
    }
    // Programmatically resizes the actual OS window (used by the Task 6
    // scaling toggle), rather than relying on the user manually dragging
    // window edges.
    void resizeWindow(int w, int h);

private:
    SDL_Window *window = nullptr;
    int width, height;
};