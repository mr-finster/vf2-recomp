#pragma once
#include <SDL3/SDL.h>
#include <functional>

struct touch_controls;

// A transparent UIKit/AppKit view above SDL's game surface. Input stays on the
// main thread and shares the same digital state as the portable touch overlay.
bool native_controls_attach(SDL_Window *window, touch_controls &input, std::function<void(int)> pressed);
void native_controls_update(bool shown, bool menu);
void native_controls_detach();
