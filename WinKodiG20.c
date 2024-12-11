// Thanks to:
/*******************************************************
 HIDAPI - Multi-Platform library for
 communication with HID devices.

 Alan Ott
 Signal 11 Software

 libusb/hidapi Team

 Copyright 2022, All Rights Reserved.

 At the discretion of the user of this library,
 this software may be licensed under the terms of the
 GNU General Public License v3, a BSD-Style license, or the
 original HIDAPI license as outlined in the LICENSE.txt,
 LICENSE-gpl3.txt, LICENSE-bsd.txt, and LICENSE-orig.txt
 files located at the root of the source distribution.
 These files may also be found in the public source
 code repository located at:
		https://github.com/libusb/hidapi .
********************************************************/

#include "stdafx.h"

#define VK_FAKE_BRIGHTNESS_DOWN VK_OEM_2
#define VK_FAKE_BRIGHTNESS_UP VK_OEM_3

static HANDLE hArrivalWaitEvent = NULL;

static HANDLE hRemote = INVALID_HANDLE_VALUE;
static OVERLAPPED overlapped = { 0, };
static BOOL read_pending = FALSE;

static INPUT inputs[8];

static HANDLE hid_open(USHORT VendorID, USHORT ProductID, USHORT UsagePage, USHORT Usage, SIZE_T ExpectedInputReportByteLength)
{
	HANDLE ret = INVALID_HANDLE_VALUE;
	CONFIGRET cr;
	LPWSTR device_interface_list = NULL;
	DWORD len;
	CONST HANDLE hHeap = GetProcessHeap();

	do {
		if ((cr = CM_Get_Device_Interface_List_SizeW(&len, (LPGUID)&GUID_DEVINTERFACE_HID, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT)) != CR_SUCCESS)
			break;

		if (device_interface_list)
			HeapFree(hHeap, 0, device_interface_list);

		device_interface_list = (LPWSTR)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, len * sizeof(WCHAR));
		if (UNLIKELY(!device_interface_list))
			return ret;

		cr = CM_Get_Device_Interface_ListW((LPGUID)&GUID_DEVINTERFACE_HID, NULL, device_interface_list, len, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
	} while (cr == CR_BUFFER_SMALL);

	if (LIKELY(cr == CR_SUCCESS)) {
		for (LPWSTR device_interface = device_interface_list; *device_interface; device_interface += wcslen(device_interface) + 1) {
			HIDD_ATTRIBUTES attrib;
			CONST HANDLE device_handle = CreateFileW(device_interface, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

			if (device_handle == INVALID_HANDLE_VALUE)
				continue;

			attrib.Size = sizeof(HIDD_ATTRIBUTES);
			if (HidD_GetAttributes(device_handle, &attrib) && attrib.VendorID == VendorID && attrib.ProductID == ProductID) {
				PHIDP_PREPARSED_DATA pp_data;
				if (LIKELY(HidD_GetPreparsedData(device_handle, &pp_data))) {
					HIDP_CAPS caps;
					if (LIKELY(HidP_GetCaps(pp_data, &caps) == HIDP_STATUS_SUCCESS)) {
						if (caps.UsagePage == UsagePage && caps.Usage == Usage) {
							if (UNLIKELY(caps.InputReportByteLength != ExpectedInputReportByteLength))
								ExitProcess(EXIT_FAILURE);
							ret = device_handle;
						}
					}

					HidD_FreePreparsedData(pp_data);

					if (ret != INVALID_HANDLE_VALUE)
						break;
				}
			}

			CloseHandle(device_handle);
		}
	}

	HeapFree(hHeap, 0, device_interface_list);
	return ret;
}

static INT hid_read_timeout(LPVOID lpBuffer, DWORD nNumberOfBytesToRead, DWORD dwTimeoutMs)
{
	DWORD numberOfBytesRead = 0;
	BOOL res = FALSE;
	BOOL bOverlapped = FALSE;

	if (!read_pending) {
		read_pending = TRUE;
		ResetEvent(overlapped.hEvent);

		if (!(res = ReadFile(hRemote, lpBuffer, nNumberOfBytesToRead, &numberOfBytesRead, &overlapped))) {
			if (GetLastError() != ERROR_IO_PENDING) {
				CancelIo(hRemote);
				read_pending = FALSE;
				goto end_of_function;
			}
			bOverlapped = TRUE;
		}
	} else {
		bOverlapped = TRUE;
	}

	if (bOverlapped) {
		if (WaitForSingleObject(overlapped.hEvent, dwTimeoutMs) != WAIT_OBJECT_0)
			return 0;

		res = GetOverlappedResult(hRemote, &overlapped, &numberOfBytesRead, FALSE);
	}
	read_pending = FALSE;

end_of_function:
	return res ? (INT)numberOfBytesRead : -1;
}

static DWORD CALLBACK cmNotifyCallback(GNUC_UNUSED HCMNOTIFICATION hNotify, GNUC_UNUSED PVOID Context, CM_NOTIFY_ACTION Action, GNUC_UNUSED PCM_NOTIFY_EVENT_DATA EventData, GNUC_UNUSED DWORD EventDataSize)
{
	if (Action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL && hRemote == INVALID_HANDLE_VALUE)
		SetEvent(hArrivalWaitEvent);

	return ERROR_SUCCESS;
}

static HWND KodiHwnd(VOID)
{
	return FindWindowW(L"Kodi", L"Kodi");
}

static HWND SpotifyHwnd(VOID)
{
	LPCWSTR CONST lpwszWantedClass = L"Chrome_WidgetWin_1";
	static HWND hWndRet = NULL;
	if (hWndRet) {
		WCHAR wszClass[20];
		assert(ARRAYSIZE(wszClass) >= wcslen(lpwszWantedClass) + 1);
		if (GetClassNameW(hWndRet, wszClass, ARRAYSIZE(wszClass)) && LIKELY(!wcscmp(wszClass, lpwszWantedClass)))
			return hWndRet;
		hWndRet = NULL;
	}

	for (HWND hWndChildAfter = NULL; (hWndChildAfter = FindWindowExW(NULL, hWndChildAfter, lpwszWantedClass, NULL));) {
		DWORD dwProcessId;
		WCHAR wszExeName[MAX_PATH];

		if (UNLIKELY(!GetWindowThreadProcessId(hWndChildAfter, &dwProcessId)))
			continue;

		CONST HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
		if (!hProcess)
			continue;

		if (LIKELY(GetProcessImageFileNameW(hProcess, wszExeName, ARRAYSIZE(wszExeName)))) {
			LPCWSTR lpwszBasename = wcsrchr(wszExeName, L'\\');
			if (LIKELY(lpwszBasename) && !wcscmp(lpwszBasename, L"\\Spotify.exe")) {
				CloseHandle(hProcess);
				return (hWndRet = hWndChildAfter);
			}
		}

		CloseHandle(hProcess);
	}

	return NULL;
}

static VOID SendAppCommand(CONST HWND hWnd, USHORT usAppCommand)
{
	if (LIKELY(hWnd))
		SendNotifyMessageW(hWnd, WM_APPCOMMAND, (WPARAM)hWnd, MAKELPARAM(0, usAppCommand | FAPPCOMMAND_OEM));
}

static VOID PauseSpotify(VOID)
{
	CONST HWND hWndSpotify = SpotifyHwnd();
	if (hWndSpotify && GetWindowTextLengthW(hWndSpotify) != 7) // "Spotify" (presumably)
		SendAppCommand(hWndSpotify, APPCOMMAND_MEDIA_STOP);
}

static BOOL ForegroundIsKodi(VOID)
{
	CONST HWND hWndKodi = KodiHwnd();
	return hWndKodi && hWndKodi == GetForegroundWindow();
}

static VOID Send(CONST WORD wVk, CONST BOOL bRelease, CONST BOOL bQuick)
{
	CONST INT inputCount = bQuick ? 2 : 1;
	ZeroMemory(inputs, sizeof(*inputs) * inputCount);

	inputs[0].type = INPUT_KEYBOARD;
	inputs[0].ki.wVk = wVk;

	if (bQuick) {
		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = wVk;
		inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
	} else if (bRelease) {
		inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
	}

	SendInput(inputCount, inputs, sizeof(*inputs));
}

static VOID QuickSendMod(CONST WORD wVk, CONST BOOL bCtrl, CONST BOOL bShift, CONST BOOL bAlt)
{
	ZeroMemory(inputs, sizeof(*inputs) * ((1 + !!bCtrl + !!bShift + !!bAlt) * 2));
	INT inputCount = 0;

	if (bCtrl) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_CONTROL;
		++inputCount;
	}

	if (bShift) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_SHIFT;
		++inputCount;
	}

	if (bAlt) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_MENU;
		++inputCount;
	}

	inputs[inputCount].type = INPUT_KEYBOARD;
	inputs[inputCount].ki.wVk = wVk;
	++inputCount;

	inputs[inputCount].type = INPUT_KEYBOARD;
	inputs[inputCount].ki.wVk = wVk;
	inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
	++inputCount;

	if (bAlt) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_MENU;
		inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
		++inputCount;
	}

	if (bShift) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_SHIFT;
		inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
		++inputCount;
	}

	if (bCtrl) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_CONTROL;
		inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
		++inputCount;
	}

	SendInput(inputCount, inputs, sizeof(*inputs));
}

static VOID SendHandleFake(CONST WORD wVk, CONST BOOL bRelease)
{
	switch (wVk)
	{
	default:
		Send(wVk, bRelease, FALSE);
		return;
	case VK_FAKE_BRIGHTNESS_DOWN:
	case VK_FAKE_BRIGHTNESS_UP:
		if (!bRelease)
			SetBrightness(wVk == VK_FAKE_BRIGHTNESS_DOWN ? -10 : 10);
		return;
	}
}

static VOID cycleKodiInfo(VOID)
{
	static INT phase_cycle = -1;

	switch (++phase_cycle)
	{
	case 0: // Player debug info
	case 1: // Video debug info
		QuickSendMod('O', TRUE, TRUE, FALSE);
		if (phase_cycle == 0)
			break;
		// fall through
	case 2:
		QuickSendMod('O', FALSE, FALSE, TRUE);
		if (phase_cycle == 1)
			break;
		// fall through
	default:
		phase_cycle = -1;
		break;
	}
}

static VOID StartProgramW(LPCWSTR lpApplicationName, LPWSTR lpCommandLine, LPCWSTR lpCurrentDirectory)
{
	STARTUPINFOW si = { .cb = sizeof(STARTUPINFOW) };
	PROCESS_INFORMATION pi;
	if (!CreateProcessW(lpApplicationName, lpCommandLine, NULL, NULL, FALSE, 0, NULL, lpCurrentDirectory, &si, &pi))
		return;

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
}

static VOID connectHeadset(BOOL bSkipReconnect)
{
	// https://github.com/qwerty12/ConnectSonyBluetoothHeadset
	#define AUTOHOTKEY_EXE_BASENAME L"AutoHotkey.exe"
	#define AUTOHOTKEY_COMMON_START AUTOHOTKEY_EXE_BASENAME L" /ErrorStdOut "
	#define CONNECTSONYHEADSET_PATH L"\"D:\\Strm\\syncthing\\backups\\ConnectSonyBluetoothHeadset\\ConnectSonyHeadset.ahk\""
	StartProgramW(L"C:\\Program Files\\AutoHotkey\\" AUTOHOTKEY_EXE_BASENAME, 
				  bSkipReconnect ? (WCHAR[]) { AUTOHOTKEY_COMMON_START CONNECTSONYHEADSET_PATH L" /skipreconnect" }
								 : (WCHAR[]) { AUTOHOTKEY_COMMON_START L"/restart " CONNECTSONYHEADSET_PATH },
				  NULL);
}

static VOID minimiseTerminateKodi(VOID)
{
	HWND hWndKodi = KodiHwnd();
	if (hWndKodi) {
		if (LIKELY(!IsHungAppWindow(hWndKodi))) {
			ShowWindow(hWndKodi, SW_MINIMIZE);
		} else {
			static HWND (WINAPI *HungWindowFromGhostWindow)(HWND) = NULL;
			if (!HungWindowFromGhostWindow) {
#if __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif
				*(FARPROC*)&HungWindowFromGhostWindow = GetProcAddress(GetModuleHandleW(L"user32.dll"), "HungWindowFromGhostWindow");
#if __GNUC__
#pragma GCC diagnostic pop
#endif
			}

			CONST HWND hWndAntiGhost = HungWindowFromGhostWindow(hWndKodi);
			if (hWndAntiGhost)
				hWndKodi = hWndAntiGhost;

			DWORD dwProcessId;
			if (UNLIKELY(!GetWindowThreadProcessId(hWndKodi, &dwProcessId)))
				return;

			CONST HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, dwProcessId);
			if (UNLIKELY(!hProcess))
				return;
			TerminateProcess(hProcess, EXIT_FAILURE);
			CloseHandle(hProcess);
		}
	} else {
		PrimaryDisplayOff();
		SetSuspendState(FALSE, TRUE, TRUE);
	}
}

static VOID startStopKodi(VOID)
{
	CONST HWND hWndKodi = KodiHwnd();
	if (hWndKodi) {
		if (GetForegroundWindow() != hWndKodi) {
			ShowWindow(hWndKodi, SW_RESTORE);
			ZeroMemory(inputs, sizeof(*inputs));
			SendInput(1, inputs, sizeof(*inputs));
			SetForegroundWindow(hWndKodi);
		} else {
			Send('S', FALSE, TRUE);
		}
		return;
	}

	StartProgramW(L"C:\\Program Files\\Kodi\\kodi.exe", (WCHAR[]) { L"kodi.exe --fullscreen" }, L"C:\\Program Files\\Kodi\\");
	PauseSpotify();
}

static VOID handle_last_key_release(CONST WORD last_key, CONST BOOL bOnlyRelease)
{
	switch (last_key)
	{
	default:
		Send(last_key, TRUE, FALSE);
		break;
	case VK_SLEEP:
		if (LIKELY(!bOnlyRelease))
			startStopKodi();
		break;
	case VK_LAUNCH_MEDIA_SELECT:
		if (LIKELY(!bOnlyRelease))
			connectHeadset(TRUE);
		break;
	case (WORD)'I':
		if (LIKELY(!bOnlyRelease))
			Send(last_key, FALSE, TRUE);
		break;
	case (WORD)'Z':
		if (LIKELY(!bOnlyRelease))
			cycleKodiInfo();
		break;
	case VK_MEDIA_STOP:
		break;
	}
}

#if defined(_WINDOWS)
INT APIENTRY wWinMain(GNUC_UNUSED HINSTANCE hInstance, GNUC_UNUSED HINSTANCE hPrevInstance, GNUC_UNUSED LPWSTR lpCmdLine, GNUC_UNUSED INT nShowCmd)
#elif defined (_CONSOLE)
INT main(VOID)
#endif
{
	HCMNOTIFICATION cmNotifyContext = NULL;
	hArrivalWaitEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

	CM_NOTIFY_FILTER cmNotifyFilter = { 0, };
	cmNotifyFilter.cbSize = sizeof(cmNotifyFilter);
	cmNotifyFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
	cmNotifyFilter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_HID;

	CHAR buf[5];
	for (;;) {
		ResetEvent(hArrivalWaitEvent);
		hRemote = hid_open(0x0957, 0x0007, 0x000c, 0x0001, sizeof(buf));
		if (hRemote == INVALID_HANDLE_VALUE) {
			if (!cmNotifyContext && UNLIKELY(CM_Register_Notification(&cmNotifyFilter, NULL, cmNotifyCallback, &cmNotifyContext) != CR_SUCCESS))
				return EXIT_FAILURE;

			if (UNLIKELY(WaitForSingleObject(hArrivalWaitEvent, INFINITE) != WAIT_OBJECT_0))
				return EXIT_FAILURE;

			continue;
		}

		WORD last_key = 0;
		DWORD dwTimeoutMs = INFINITE;
		for (;;) {
			CONST INT bytes_read = hid_read_timeout(buf, sizeof(buf), dwTimeoutMs);
#if defined(_DEBUG) && defined(_CONSOLE)
			printf("bytes: %d last_key: %d\n", bytes_read, last_key);
			for (INT i = 0; i < bytes_read; ++i)
				printf("buf[%d]: %d\n", i, buf[i]);
#endif

			if (bytes_read == -1)
				break;

			switch (bytes_read)
			{
			case 0: // timeout reached if dwTimeoutMs != INFINITE - key not released
				switch (last_key)
				{
				default:
					SendHandleFake(last_key, FALSE);
					if (dwTimeoutMs != 100) // enable quick repeats
						dwTimeoutMs = 100;
					break;
				case VK_SLEEP:
					last_key = 0;
					minimiseTerminateKodi();
					break;
				case VK_LAUNCH_MEDIA_SELECT:
					last_key = 0;
					connectHeadset(FALSE);
					break;
				case (WORD)'I':
					last_key = 'O';
					// fall through
				case (WORD)'Z':
				case VK_MEDIA_STOP:
					Send(last_key, FALSE, TRUE);
					last_key = 0;
					break;
				case 0:
					break;
				}

				if (last_key == 0)
					dwTimeoutMs = INFINITE;
				continue;
			case sizeof(buf):
				break;
			default:
				continue;
			}

			if (UNLIKELY(buf[0] != 2 || buf[3] != 0 || buf[4] != 0)) // usually [2] is 0, but sometimes 2 for some keys :shrug:
				continue;

			#define MAP_KEYPRESS(input, virtual_key) case input: last_key = (WORD)virtual_key; break;
			#define MAP_LONGPRESS_CUSTOM(input, virtual_key) case input: last_key = (WORD)virtual_key; goto check_longpress;
			#define MAP_FUNCCALL(input, func, ...) case input: func(__VA_ARGS__); continue;
			#define MAP_KEYPRESS_QUICK(input, virtual_key) MAP_FUNCCALL(input, Send, virtual_key, FALSE, TRUE)
			#define MAP_KEYPRESS_APPCOMMAND(input, appcommand) MAP_FUNCCALL(input, SendAppCommand, KodiHwnd(), appcommand)
			switch (buf[1])
			{
			case 0: // (any key released)
				if (last_key != 0) {
					handle_last_key_release(last_key, FALSE);
					last_key = 0;
				}
				if (dwTimeoutMs != INFINITE)
					dwTimeoutMs = INFINITE;
				continue;
			MAP_KEYPRESS(65, VK_RETURN) // D-pad center
			MAP_KEYPRESS(66, VK_UP) // D-pad up
			MAP_KEYPRESS(67, VK_DOWN) // D-pad down
			MAP_KEYPRESS(68, VK_LEFT) // D-pad left
			MAP_KEYPRESS(69, VK_RIGHT) // D-pad right
			MAP_KEYPRESS_QUICK(-106, VK_MEDIA_PLAY_PAUSE) // Settings
			MAP_KEYPRESS(-100, VK_VOLUME_UP) // Arrow up
			MAP_KEYPRESS(-99, VK_VOLUME_DOWN) // Arrow down
			MAP_KEYPRESS(107, VK_TAB) // Blue
			MAP_KEYPRESS(97, 'T') // Subtitles
			MAP_LONGPRESS_CUSTOM(-67, 'I') // Info
			MAP_KEYPRESS_QUICK(122, ForegroundIsKodi() ? VK_NEXT : VK_MEDIA_NEXT_TRACK) // Google Play
			MAP_KEYPRESS_QUICK(121, ForegroundIsKodi() ? VK_PRIOR : VK_MEDIA_PREV_TRACK) // Prime Video
			MAP_LONGPRESS_CUSTOM(-115, VK_MEDIA_STOP) // Guide
			MAP_KEYPRESS(42, VK_F15) // Bookmarks
			MAP_LONGPRESS_CUSTOM(108, 'Z') // Yellow
			MAP_LONGPRESS_CUSTOM(48, VK_SLEEP) // Power
			MAP_LONGPRESS_CUSTOM(-69, VK_LAUNCH_MEDIA_SELECT) // Input
			MAP_KEYPRESS(35, VK_HOME) // Home
			MAP_KEYPRESS(119, VK_FAKE_BRIGHTNESS_DOWN) // YouTube
			MAP_KEYPRESS(120, VK_FAKE_BRIGHTNESS_UP) // Netflix
			MAP_KEYPRESS(105, VK_F13) // Red
			MAP_KEYPRESS(106, VK_F14) // Green
			default: continue;
			}

			if (LIKELY(last_key != 0)) {
				SendHandleFake(last_key, FALSE);
check_longpress:
				dwTimeoutMs = 500;
			}
		}

		read_pending = FALSE;
		CloseHandle(hRemote);
		if (last_key != 0)
			handle_last_key_release(last_key, TRUE);
	}

	CM_Unregister_Notification(cmNotifyContext);
	CloseHandle(hArrivalWaitEvent);
	CloseHandle(overlapped.hEvent);
	ClearPhysicalMonitors();
	return EXIT_SUCCESS;
}
