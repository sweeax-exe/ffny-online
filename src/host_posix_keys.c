/*
 * host_posix_keys.c -- SDL's keys as the Windows virtual-key codes the input layer's bindings are
 * written in. Shared by the game's window (host_posix.c) and the app's (src/app), whose Controls
 * page learns a binding from a key press.
 */
#include <SDL3/SDL.h>

int host_posix_vk_from_scancode(int scancode);

/* A key by where it is on the keyboard, as the Windows virtual-key code the
 * input layer's bindings are written in; 0 for a key it has no name for.
 * Shift, Ctrl and Alt come by side (VK_LSHIFT and so on). */
int host_posix_vk_from_scancode(int scancode)
{
    SDL_Scancode sc = (SDL_Scancode)scancode;

    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)        return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)        return '1' + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0)                                return '0';
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)     return 0x70 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)  return 0x61 + (sc - SDL_SCANCODE_KP_1);
    switch (sc) {
    case SDL_SCANCODE_KP_0:      return 0x60;
    case SDL_SCANCODE_RETURN:    return 0x0D;
    case SDL_SCANCODE_KP_ENTER:  return 0x0D;
    case SDL_SCANCODE_ESCAPE:    return 0x1B;
    case SDL_SCANCODE_BACKSPACE: return 0x08;
    case SDL_SCANCODE_TAB:       return 0x09;
    case SDL_SCANCODE_SPACE:     return 0x20;
    case SDL_SCANCODE_LEFT:      return 0x25;
    case SDL_SCANCODE_UP:        return 0x26;
    case SDL_SCANCODE_RIGHT:     return 0x27;
    case SDL_SCANCODE_DOWN:      return 0x28;
    case SDL_SCANCODE_LSHIFT:    return 0xA0;
    case SDL_SCANCODE_RSHIFT:    return 0xA1;
    case SDL_SCANCODE_LCTRL:     return 0xA2;
    case SDL_SCANCODE_RCTRL:     return 0xA3;
    case SDL_SCANCODE_LALT:      return 0xA4;
    case SDL_SCANCODE_RALT:      return 0xA5;
    default:                     return 0;
    }
}
