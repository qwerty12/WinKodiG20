// ==WindhawkMod==
// @id              kodi-ignore-appcommands
// @name            Kodi: Hide WM_APPCOMMANDs
// @description     So certain buttons on my remote don't trigger actions, oh and disable shutdown/reboot too
// @version         1.0
// @include         kodi.exe
// @compilerOptions -lcomctl32 -ldxva2
// ==/WindhawkMod==

// Window messaging/subclassing code ripped entirely from m417z
// Source code is published under The GNU General Public License v3.0.
//
// Look here for the original code:
// https://github.com/ramensoftware/windhawk-mods/issues

#include <commctrl.h>
#include <windows.h>
#include <powrprof.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <cwchar>

static HWND g_kodiWnd;

// wParam - TRUE to subclass, FALSE to unsubclass
// lParam - subclass data
UINT g_subclassRegisteredMsg = RegisterWindowMessage(
    L"Windhawk_SetWindowSubclassFromAnyThread_kodi-ignore-appcommands");

struct SET_WINDOW_SUBCLASS_FROM_ANY_THREAD_PARAM {
    SUBCLASSPROC pfnSubclass;
    UINT_PTR uIdSubclass;
    DWORD_PTR dwRefData;
    BOOL result;
};

LRESULT CALLBACK CallWndProcForWindowSubclass(int nCode,
                                              WPARAM wParam,
                                              LPARAM lParam)
{
    if (nCode == HC_ACTION) {
        const CWPSTRUCT* cwp = (const CWPSTRUCT*)lParam;
        if (cwp->message == g_subclassRegisteredMsg && cwp->wParam) {
            SET_WINDOW_SUBCLASS_FROM_ANY_THREAD_PARAM* param =
                (SET_WINDOW_SUBCLASS_FROM_ANY_THREAD_PARAM*)cwp->lParam;
            param->result =
                SetWindowSubclass(cwp->hwnd, param->pfnSubclass,
                                  param->uIdSubclass, param->dwRefData);
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static BOOL SetWindowSubclassFromAnyThread(HWND hWnd,
                                    SUBCLASSPROC pfnSubclass,
                                    UINT_PTR uIdSubclass,
                                    DWORD_PTR dwRefData)
{
    DWORD dwThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (dwThreadId == 0) {
        return FALSE;
    }

    if (dwThreadId == GetCurrentThreadId()) {
        return SetWindowSubclass(hWnd, pfnSubclass, uIdSubclass, dwRefData);
    }

    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, CallWndProcForWindowSubclass,
                                  nullptr, dwThreadId);
    if (!hook) {
        return FALSE;
    }

    SET_WINDOW_SUBCLASS_FROM_ANY_THREAD_PARAM param;
    param.pfnSubclass = pfnSubclass;
    param.uIdSubclass = uIdSubclass;
    param.dwRefData = dwRefData;
    param.result = FALSE;
    SendMessageW(hWnd, g_subclassRegisteredMsg, TRUE, (WPARAM)&param);

    UnhookWindowsHookEx(hook);

    return param.result;
}

LRESULT CALLBACK KodiWindowSubclassProc(_In_ HWND hWnd,
                                        _In_ UINT uMsg,
                                        _In_ WPARAM wParam,
                                        _In_ LPARAM lParam,
                                        _In_ UINT_PTR uIdSubclass,
                                        _In_ DWORD_PTR dwRefData)
{
    if (uMsg == WM_NCDESTROY || (uMsg == g_subclassRegisteredMsg && !wParam)) {
        RemoveWindowSubclass(hWnd, KodiWindowSubclassProc, 0);
    }

    switch (uMsg) {
        case WM_APPCOMMAND:
            switch (GET_APPCOMMAND_LPARAM(lParam)) {
                case APPCOMMAND_BROWSER_FAVORITES:
                case APPCOMMAND_BROWSER_HOME:
                case APPCOMMAND_MEDIA_NEXTTRACK:
                case APPCOMMAND_MEDIA_PREVIOUSTRACK:
                case APPCOMMAND_MEDIA_CHANNEL_UP:
                case APPCOMMAND_MEDIA_CHANNEL_DOWN:
                    return TRUE;
                default:
                    break;
            }

        case WM_NCDESTROY:
            g_kodiWnd = nullptr;
            break;
    }

    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static bool IsKodiWindow(HWND hWnd)
{
    WCHAR szClassName[32];
    if (!GetClassNameW(hWnd, szClassName, ARRAYSIZE(szClassName)) ||
        wcscmp(szClassName, L"Kodi") != 0) {
        return false;
    }

    return true;
}

BOOL CALLBACK InitialEnumKodiWindowsFunc(HWND hWnd, LPARAM lParam)
{
    DWORD dwProcessId = 0;
    if (!GetWindowThreadProcessId(hWnd, &dwProcessId) ||
        dwProcessId != GetCurrentProcessId()) {
        return TRUE;
    }

    if (!IsKodiWindow(hWnd)) {
        return TRUE;
    }

    Wh_Log(L"Kodi window found: %08X", (DWORD)(ULONG_PTR)hWnd);

    g_kodiWnd = hWnd;
    SetWindowSubclassFromAnyThread(hWnd, KodiWindowSubclassProc, 0, 0);

    return FALSE;
}

using CreateWindowExW_t = decltype(&CreateWindowExW);
CreateWindowExW_t pOriginalCreateWindowExW;
HWND WINAPI CreateWindowExWHook(DWORD dwExStyle,
                                LPCWSTR lpClassName,
                                LPCWSTR lpWindowName,
                                DWORD dwStyle,
                                int X,
                                int Y,
                                int nWidth,
                                int nHeight,
                                HWND hWndParent,
                                HMENU hMenu,
                                HINSTANCE hInstance,
                                LPVOID lpParam)
{
    HWND hWnd = pOriginalCreateWindowExW(dwExStyle, lpClassName, lpWindowName,
                                         dwStyle, X, Y, nWidth, nHeight,
                                         hWndParent, hMenu, hInstance, lpParam);
    if (!hWnd) {
        return hWnd;
    }

    if (!g_kodiWnd && IsKodiWindow(hWnd)) {
        Wh_Log(L"Kodi window created: %08X", (DWORD)(ULONG_PTR)hWnd);

        g_kodiWnd = hWnd;
        SetWindowSubclass(hWnd, KodiWindowSubclassProc, 0, 0);
    }

    return hWnd;
}

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdc, LPRECT lprcClip, LPARAM dwData)
{
    DWORD cPhysicalMonitors = 0;
    PHYSICAL_MONITOR physicalMonitors[8]; // CBA with allocing for an unlikely case

    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &cPhysicalMonitors))
        return TRUE;

    if (cPhysicalMonitors == 0 && cPhysicalMonitors > ARRAYSIZE(physicalMonitors))
        return TRUE;

    if (GetPhysicalMonitorsFromHMONITOR(hMonitor, cPhysicalMonitors, physicalMonitors)) {
        for (DWORD i = 0; i < cPhysicalMonitors; ++i) {
            CONST HANDLE hPhysicalMonitor = physicalMonitors[i].hPhysicalMonitor;
            //SetVCPFeature(hPhysicalMonitor, 0xD6, 0x04); // Off
            for (DWORD j = 0; j < 3; ++j) {
                if (!SetVCPFeature(hPhysicalMonitor, 0xD6, 0x05)) // HardOff
                    break;
            }
            DestroyPhysicalMonitor(hPhysicalMonitor);
        }
    }

    return TRUE;
}

static VOID DisplaysOff()
{
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, 0);
}

using SetSuspendState_t = decltype(&SetSuspendState);
SetSuspendState_t pOriginalSetSuspendState;
BOOLEAN WINAPI SetSuspendStateHook(BOOLEAN bHibernate,
                                   BOOLEAN bForce,
                                   BOOLEAN bWakeupEventsDisabled)
{
    if (!bHibernate && bForce && !bWakeupEventsDisabled) {
        INPUT inputs[2];
        ZeroMemory(inputs, sizeof(inputs));

        inputs[0].type = inputs[1].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = inputs[1].ki.wVk = VK_ESCAPE;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

        DisplaysOff();
    	SendInput(ARRAYSIZE(inputs), inputs, sizeof(*inputs));
    }

    return FALSE;
}

DWORD WINAPI InitiateShutdownWHook(LPWSTR lpMachineName,
                                   LPWSTR lpMessage,
                                   DWORD dwGracePeriod,
                                   DWORD dwShutdownFlags,
                                   DWORD dwReason)
{
    if (dwShutdownFlags & (SHUTDOWN_POWEROFF | SHUTDOWN_RESTART)) {
        //if (g_kodiWnd)
        //    PostMessageW(g_kodiWnd, WM_CLOSE, 0, 0);
        if (dwShutdownFlags & SHUTDOWN_RESTART)
            DisplaysOff();
        pOriginalSetSuspendState(FALSE, TRUE, TRUE);
        return ERROR_SUCCESS;
    }
    return ERROR_ACCESS_DENIED;
}

static HANDLE hStringsPo = INVALID_HANDLE_VALUE;
using CreateFileW_t = decltype(&CreateFileW);
CreateFileW_t pOriginalCreateFileW;
HANDLE WINAPI CreateFileWHook(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    CONST HANDLE hRet = pOriginalCreateFileW(
        lpFileName,
        dwDesiredAccess,
        dwShareMode,
        lpSecurityAttributes,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        hTemplateFile
    );

    if (hStringsPo == INVALID_HANDLE_VALUE && hRet != INVALID_HANDLE_VALUE &&
        dwCreationDisposition == OPEN_EXISTING &&
        dwDesiredAccess == GENERIC_READ &&
        dwShareMode & (FILE_SHARE_READ | FILE_SHARE_WRITE) &&
        dwFlagsAndAttributes == FILE_ATTRIBUTE_NORMAL &&
        !lpSecurityAttributes && !hTemplateFile &&
        wcscmp(lpFileName, L"\\\\?\\C:\\Program Files\\Kodi\\addons\\resource.language.en_gb\\resources\\strings.po") == 0)
    {
        hStringsPo = hRet;
        Wh_RemoveFunctionHook((void*)CreateFileW);
    }

    return hRet;
}

using ReadFile_t = decltype(&ReadFile);
ReadFile_t pOriginalReadFile;
WINBOOL WINAPI ReadFileHook(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    CONST BOOL bRet = pOriginalReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
    if (hStringsPo != INVALID_HANDLE_VALUE && bRet && hFile == hStringsPo) {
        struct
        {
            LPCSTR lpTarget;
            LPCSTR lpReplacement;
        } CONST replacements[] = {
            { R"(msgid "Power off system")", R"(msgid " Suspend system ")" },
            { R"(msgid "Reboot")", R"(msgid "Sleep^")" },
            { R"(msgid "Suspend")", R"(msgid "DispOff")" },
        };
        for (SIZE_T i = 0; i < ARRAYSIZE(replacements); ++i) {
            LPSTR lpSub = strstr((char*)lpBuffer, replacements[i].lpTarget);
            if (lpSub)
                memcpy(lpSub, replacements[i].lpReplacement, strlen(replacements[i].lpReplacement));
        }

        Wh_RemoveFunctionHook((void*)ReadFile);
        Wh_ApplyHookOperations();
        hStringsPo = INVALID_HANDLE_VALUE;
    }

    return bRet;
}

BOOL Wh_ModInit()
{
    Wh_Log(L">");

    Wh_SetFunctionHook((void*)CreateWindowExW, (void*)CreateWindowExWHook, (void**)&pOriginalCreateWindowExW);
    Wh_SetFunctionHook((void*)GetProcAddress(LoadLibraryExW(L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "SetSuspendState"), (void*)SetSuspendStateHook, (void**)&pOriginalSetSuspendState);
    Wh_SetFunctionHook((void*)InitiateShutdownW, (void*)InitiateShutdownWHook, nullptr);

    Wh_SetFunctionHook((void*)CreateFileW, (void*)CreateFileWHook, (void**)&pOriginalCreateFileW);
    Wh_SetFunctionHook((void*)ReadFile, (void*)ReadFileHook, (void**)&pOriginalReadFile);

    return TRUE;
}

void Wh_ModAfterInit()
{
    Wh_Log(L">");

    EnumWindows(InitialEnumKodiWindowsFunc, 0);
}

void Wh_ModUninit()
{
    Wh_Log(L">");

    if (g_kodiWnd) {
        SendMessageW(g_kodiWnd, g_subclassRegisteredMsg, FALSE, 0);
    }
}
