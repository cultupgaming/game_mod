#include <main.h>

#include "keyboard.h"
#include "script.h"
#include "trace.h"

HMODULE g_moduleHandle = nullptr;

namespace
{
    // Public release builds do not expose the F6/F7/F8 execution harness or
    // the temporary F9 outfit-inspection toggle. Normal Knifemare gameplay
    // reads the game's TARGET/FIRE state directly and does not require the
    // ScriptHook keyboard message handler.
    constexpr bool KEYBOARD_HOTKEYS_ENABLED = false;
}

// The non-delayed import is the startup dependency for update\game\mapres.rpf.
// Ultimate ASI Loader exposes this lightweight probe and Windows resolves the
// dependency before the ASI's DllMain runs.
extern "C" __declspec(dllimport) bool WINAPI IsUltimateASILoader();

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_moduleHandle = hInstance;
        InitTrace(hInstance);
        Trace("DllMain: process attach");
        Trace("RDK Assassin Knife UI: imported wininet IsUltimateASILoader",
            IsUltimateASILoader() ? 1LL : 0LL);
        scriptRegister(hInstance, ScriptMain);
        if (KEYBOARD_HOTKEYS_ENABLED)
            keyboardHandlerRegister(OnKeyboardMessage);
        Trace("DllMain: script registered; release hotkeys disabled");
        break;

    case DLL_PROCESS_DETACH:
        // Gameplay cleanup intentionally stays in ScriptMain/update paths.
        // Calling game natives from DllMain is unsafe.
        if (KEYBOARD_HOTKEYS_ENABLED)
            keyboardHandlerUnregister(OnKeyboardMessage);
        scriptUnregister(hInstance);
        break;
    }

    return TRUE;
}
