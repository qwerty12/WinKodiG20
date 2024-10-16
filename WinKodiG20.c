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

#include <stdlib.h>
#include <stdio.h>

#include <windows.h>
#include <initguid.h>
#include <hidclass.h>
#include <hidsdi.h>
#include <cfgmgr32.h>
#include <Psapi.h>

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

	do {
		if ((cr = CM_Get_Device_Interface_List_SizeW(&len, (LPGUID)&GUID_DEVINTERFACE_HID, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT)) != CR_SUCCESS)
			break;

		if (device_interface_list)
			HeapFree(GetProcessHeap(), 0, device_interface_list);

		device_interface_list = (LPWSTR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(WCHAR));
		if (!device_interface_list)
			return ret;

		cr = CM_Get_Device_Interface_ListW((LPGUID)&GUID_DEVINTERFACE_HID, NULL, device_interface_list, len, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
	} while (cr == CR_BUFFER_SMALL);

	if (cr == CR_SUCCESS) {
		for (LPWSTR device_interface = device_interface_list; *device_interface; device_interface += wcslen(device_interface) + 1) {
			HIDD_ATTRIBUTES attrib;
			HANDLE device_handle = CreateFileW(device_interface, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

			if (device_handle == INVALID_HANDLE_VALUE)
				continue;

			attrib.Size = sizeof(HIDD_ATTRIBUTES);
			if (HidD_GetAttributes(device_handle, &attrib) && attrib.VendorID == VendorID && attrib.ProductID == ProductID) {
				PHIDP_PREPARSED_DATA pp_data;
				if (HidD_GetPreparsedData(device_handle, &pp_data)) {
					HIDP_CAPS caps;
					if (HidP_GetCaps(pp_data, &caps) == HIDP_STATUS_SUCCESS) {
						if (caps.UsagePage == UsagePage && caps.Usage == Usage) {
							if (caps.InputReportByteLength != ExpectedInputReportByteLength)
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

	HeapFree(GetProcessHeap(), 0, device_interface_list);
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
	}
	else {
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

static DWORD CALLBACK cmNotifyCallback(HCMNOTIFICATION hNotify, PVOID Context, CM_NOTIFY_ACTION Action, PCM_NOTIFY_EVENT_DATA EventData, DWORD EventDataSize)
{
	if (Action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL && hRemote == INVALID_HANDLE_VALUE)
		SetEvent(hArrivalWaitEvent);

	return ERROR_SUCCESS;
}

static HWND KodiHwnd()
{
	return FindWindowW(L"Kodi", L"Kodi");
}

static HWND SpotifyHwnd()
{
	LPCWSTR CONST lpwszWantedClass = L"Chrome_WidgetWin_1";
	static HWND ret = NULL;
	if (ret) {
		WCHAR wszClass[20];
		if (GetClassNameW(ret, wszClass, ARRAYSIZE(wszClass)) && !wcscmp(wszClass, lpwszWantedClass))
			return ret;
		ret = NULL;
	}

	HWND hWndChildAfter = NULL;
	while ((hWndChildAfter = FindWindowExW(NULL, hWndChildAfter, lpwszWantedClass, NULL))) {
		DWORD dwProcessId;
		WCHAR wszExeName[MAX_PATH];

		if (!GetWindowThreadProcessId(hWndChildAfter, &dwProcessId))
			continue;

		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
		if (!hProcess)
			continue;

		if (GetProcessImageFileNameW(hProcess, wszExeName, ARRAYSIZE(wszExeName))) {
			LPCWSTR lpwszBasename = wcsrchr(wszExeName, L'\\');
			if (lpwszBasename && !wcscmp(lpwszBasename, L"\\Spotify.exe")) {
				ret = hWndChildAfter;
				CloseHandle(hProcess);
				return ret;
			}
		}

		CloseHandle(hProcess);
	}

	return NULL;
}

static VOID SendAppCommand(CONST HWND hWnd, USHORT usAppCommand)
{
	if (hWnd)
		SendNotifyMessage(hWnd, WM_APPCOMMAND, (WPARAM)hWnd, MAKELPARAM(0, usAppCommand | FAPPCOMMAND_OEM));
}

static VOID PauseSpotify()
{
	CONST HWND hWndSpotify = SpotifyHwnd();
	if (hWndSpotify && GetWindowTextLengthW(hWndSpotify) != 7) // "Spotify" (presumably)
		SendAppCommand(hWndSpotify, APPCOMMAND_MEDIA_PAUSE);
}

static VOID Send(WORD wVk, BOOL bRelease, BOOL bQuick)
{
	CONST INT inputCount = bQuick ? 2 : 1;
	ZeroMemory(inputs, sizeof(*inputs) * inputCount);

	inputs[0].type = INPUT_KEYBOARD;
	inputs[0].ki.wVk = wVk;

	if (bQuick)
	{
		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = wVk;
		inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;
	}
	else if (bRelease)
	{
		inputs[0].ki.dwFlags |= KEYEVENTF_KEYUP;
	}

	SendInput(inputCount, inputs, sizeof(*inputs));
}

static VOID QuickSendMod(WORD wVk, BOOL bCtrl, BOOL bShift, BOOL bAlt) {
	ZeroMemory(inputs, sizeof(inputs));
	INT inputCount = 0;

	if (bCtrl) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_CONTROL;
		inputCount++;
	}

	if (bShift) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_SHIFT;
		inputCount++;
	}

	if (bAlt) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_MENU;
		inputCount++;
	}

	inputs[inputCount].type = INPUT_KEYBOARD;
	inputs[inputCount].ki.wVk = wVk;
	inputCount++;

	inputs[inputCount].type = INPUT_KEYBOARD;
	inputs[inputCount].ki.wVk = wVk;
	inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
	inputCount++;

	if (bAlt) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_MENU;
		inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
		inputCount++;
	}

	if (bShift) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_SHIFT;
		inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
		inputCount++;
	}

	if (bCtrl) {
		inputs[inputCount].type = INPUT_KEYBOARD;
		inputs[inputCount].ki.wVk = VK_CONTROL;
		inputs[inputCount].ki.dwFlags = KEYEVENTF_KEYUP;
		inputCount++;
	}

	SendInput(inputCount, inputs, sizeof(*inputs));
}

static VOID cycleKodiInfo()
{
	static INT phase_cycle = -1;

	switch (++phase_cycle)
	{
	case 0: // Player debug info
	case 1: // Video debug info
		QuickSendMod('O', TRUE, TRUE, FALSE);
		if (phase_cycle == 0)
			break;
	case 2:
		QuickSendMod('O', FALSE, FALSE, TRUE);
		if (phase_cycle == 1)
			break;
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

static VOID connectHeadset()
{
	// https://github.com/qwerty12/ConnectSonyBluetoothHeadset
	WCHAR wstrCommandLine[] = L"AutoHotkey.exe \"D:\\Strm\\syncthing\\backups\\ConnectSonyBluetoothHeadset\\ConnectSonyHeadset.ahk\"";
	StartProgramW(L"C:\\Program Files\\AutoHotkey\\AutoHotkey.exe", wstrCommandLine, NULL);
}

static VOID startStopKodi()
{
	CONST HWND hWndKodi = KodiHwnd();
	if (hWndKodi) {
		if (GetForegroundWindow() != hWndKodi) {
			ShowWindow(hWndKodi, SW_RESTORE);
			ZeroMemory(inputs, sizeof(*inputs));
			SendInput(1, inputs, sizeof(*inputs));
			SetForegroundWindow(hWndKodi);
		}
		else {
			Send('S', FALSE, TRUE);
		}
		return;
	}

	StartProgramW(L"C:\\Program Files\\Kodi\\kodi.exe", NULL, L"C:\\Program Files\\Kodi\\");
	PauseSpotify();
}

static VOID handle_last_key_release(WORD last_key, BOOL bOnlyRelease)
{
	switch (last_key)
	{
	case (WORD)'I':
		if (!bOnlyRelease)
			Send(last_key, FALSE, TRUE);
		break;
	case (WORD)'Z':
		if (!bOnlyRelease)
			cycleKodiInfo();
		break;
	case (WORD)'X':
		break;
	default:
		Send(last_key, TRUE, FALSE);
		break;
	}
}

#if defined(_WINDOWS)
INT APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nShowCmd)
#elif defined (_CONSOLE)
INT main(VOID)
#endif
{
	HCMNOTIFICATION cmNotifyContext = 0;
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
			if (!cmNotifyContext && CM_Register_Notification(&cmNotifyFilter, NULL, cmNotifyCallback, &cmNotifyContext) != CR_SUCCESS)
				return EXIT_FAILURE;

			if (WaitForSingleObject(hArrivalWaitEvent, INFINITE) != WAIT_OBJECT_0)
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
				case 0:
					break;
				case (WORD)'I':
					last_key = 'O';
				case (WORD)'Z':
				case (WORD)'X':
					Send(last_key, FALSE, TRUE);
					last_key = 0;
					break;
				default:
					Send(last_key, FALSE, FALSE);
					if (dwTimeoutMs != 100) // enable quick repeats
						dwTimeoutMs = 100;
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

			if (buf[0] != 2 || buf[3] != 0 || buf[4] != 0) // usually [2] is 0, but sometimes 2 for some keys :shrug:
				continue;

			#define MAP_KEYPRESS(input, virtual_key) case input: last_key = (WORD)virtual_key; break;
			#define MAP_LONGPRESS_CUSTOM(input, virtual_key) case input: last_key = (WORD)virtual_key; goto check_longpress;
			#define MAP_FUNCCALL(input, func, ...) case input: func(##__VA_ARGS__); continue;
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
			MAP_KEYPRESS_QUICK(-99, VK_VOLUME_DOWN) // Arrow down
			MAP_KEYPRESS(107, VK_TAB) // Blue
			MAP_KEYPRESS(97, 'T') // Subtitles
			MAP_LONGPRESS_CUSTOM(-67, 'I') // Info
			MAP_KEYPRESS(122, VK_NEXT) // Google Play
			MAP_LONGPRESS_CUSTOM(-115, 'X') // Guide
			MAP_KEYPRESS(121, VK_PRIOR) // Prime Video
			MAP_KEYPRESS(42, VK_F15) // Bookmarks
			MAP_LONGPRESS_CUSTOM(108, 'Z') // Yellow
			MAP_FUNCCALL(48, startStopKodi) // Power
			MAP_FUNCCALL(-69, connectHeadset) // Input
			MAP_KEYPRESS(35, VK_HOME) // Home
			MAP_KEYPRESS_APPCOMMAND(119, APPCOMMAND_MEDIA_REWIND) // YouTube
			MAP_KEYPRESS_APPCOMMAND(120, APPCOMMAND_MEDIA_FAST_FORWARD) // Netflix
			MAP_KEYPRESS(105, VK_F13) // Red
			MAP_KEYPRESS(106, VK_F14) // Green
			default: continue;
			}
			#undef MAP_KEYPRESS
			#undef MAP_KEYPRESS_QUICK
			#undef MAP_FUNCCALL
			#undef MAP_LONGPRESS_CUSTOM
			#undef MAP_KEYPRESS_APPCOMMAND

			if (last_key != 0) {
				Send(last_key, FALSE, FALSE);
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
	return EXIT_SUCCESS;
}