#include "keyboard.h"

#include <cstring>

namespace
{
    constexpr int KEY_COUNT = 255;
    constexpr ULONGLONG JUST_UP_WINDOW_MS = 120;

    struct KeyState
    {
        ULONGLONG time = 0;
        BOOL isUpNow = TRUE;
    };

    KeyState g_keyStates[KEY_COUNT] = {};
}

void OnKeyboardMessage(
    DWORD key,
    WORD,
    BYTE,
    BOOL,
    BOOL,
    BOOL,
    BOOL isUpNow)
{
    if (key >= KEY_COUNT)
        return;

    g_keyStates[key].time = GetTickCount64();
    g_keyStates[key].isUpNow = isUpNow;
}

bool IsKeyDown(DWORD key)
{
    if (key >= KEY_COUNT)
        return false;

    return g_keyStates[key].isUpNow == FALSE;
}

bool IsKeyJustUp(DWORD key, bool exclusive)
{
    if (key >= KEY_COUNT)
        return false;

    const bool justUp =
        g_keyStates[key].isUpNow
        && GetTickCount64() < g_keyStates[key].time + JUST_UP_WINDOW_MS;

    if (justUp && exclusive)
        ResetKeyState(key);

    return justUp;
}

void ResetKeyState(DWORD key)
{
    if (key >= KEY_COUNT)
        return;

    std::memset(&g_keyStates[key], 0, sizeof(g_keyStates[key]));
    g_keyStates[key].isUpNow = TRUE;
}
