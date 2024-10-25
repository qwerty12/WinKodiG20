// ==WindhawkMod==
// @id              hide-camera-notification
// @name            Don't show Camera enabled/disabled notification
// @description     Hide the notification that's shown when, apparently, some camera switch is flicked (and the Netflix button on my remote...)
// @version         0.1
// @author          qwerty12
// @github          https://github.com/qwerty12
// @include         explorer.exe
// ==/WindhawkMod==

// Thanks to https://github.com/ramensoftware/windhawk-mods/blob/main/mods/classic-clock-button-behavior.wh.cpp

#include <windhawk_utils.h>

HRESULT (*CHardwareButtonFlyout_ShowFlyout_orig)(LPVOID, INT, DWORD);
HRESULT CHardwareButtonFlyout_ShowFlyout_hook(LPVOID lpThis, INT a2, DWORD a3)
{
    if ((a2 != 5) || (a3 != 2 && a3 != 3))
        return CHardwareButtonFlyout_ShowFlyout_orig(lpThis, a2, a3);
    return S_OK;
}

static CONST WindhawkUtils::SYMBOL_HOOK twinuiHooks[] = {
    {
        {
            L"public: long __cdecl CHardwareButtonFlyout::ShowFlyout(int,unsigned long)"
        },
        &CHardwareButtonFlyout_ShowFlyout_orig,
        &CHardwareButtonFlyout_ShowFlyout_hook,
        false
    }
};

BOOL Wh_ModInit() {
    Wh_Log(L"Init");

    CONST HMODULE hModuleTwinui = LoadLibraryExW(L"twinui.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hModuleTwinui) {
        Wh_Log(L"Failed to load twinui.dll");
        return FALSE;
    }

    if (!WindhawkUtils::HookSymbols(hModuleTwinui, twinuiHooks, ARRAYSIZE(twinuiHooks))) {
        Wh_Log(L"Failed to hook one or more symbol functions");
        return FALSE;
    }

    return TRUE;
}
