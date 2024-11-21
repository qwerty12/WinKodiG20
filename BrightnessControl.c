#include "stdafx.h"

static DWORD cPhysicalMonitors = 0;
static LPPHYSICAL_MONITOR lpPhysicalMonitors = NULL;

VOID ClearPhysicalMonitors(VOID)
{
	if (lpPhysicalMonitors) {
		if (LIKELY(cPhysicalMonitors))
			DestroyPhysicalMonitors(cPhysicalMonitors, lpPhysicalMonitors);
		HeapFree(GetProcessHeap(), 0, lpPhysicalMonitors);
		lpPhysicalMonitors = NULL;
	}

	cPhysicalMonitors = 0;
}

static VOID PopulatePhysicalPrimaryMonitor(VOID)
{
	if (UNLIKELY(lpPhysicalMonitors))
		return;

	CONST HMONITOR hMonitor = MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY);
	if (!hMonitor)
		return;

	if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &cPhysicalMonitors) || !cPhysicalMonitors)
		return;

	lpPhysicalMonitors = (LPPHYSICAL_MONITOR)HeapAlloc(GetProcessHeap(), 0, sizeof(PHYSICAL_MONITOR) * cPhysicalMonitors);
	if (UNLIKELY(!lpPhysicalMonitors)) {
		cPhysicalMonitors = 0;
		return;
	}

	if (UNLIKELY(!GetPhysicalMonitorsFromHMONITOR(hMonitor, cPhysicalMonitors, lpPhysicalMonitors))) {
		cPhysicalMonitors = 0;
		ClearPhysicalMonitors();
	}
}

VOID SetBrightness(INT iIncOrDecrement)
{
	DWORD dwMinimumBrightness, dwCurrentBrightness, dwMaximumBrightness;

	if (!cPhysicalMonitors || !GetMonitorBrightness(lpPhysicalMonitors[0].hPhysicalMonitor, &dwMinimumBrightness, &dwCurrentBrightness, &dwMaximumBrightness)) {
		ClearPhysicalMonitors();
		PopulatePhysicalPrimaryMonitor();
		if (UNLIKELY(!cPhysicalMonitors || !GetMonitorBrightness(lpPhysicalMonitors[0].hPhysicalMonitor, &dwMinimumBrightness, &dwCurrentBrightness, &dwMaximumBrightness)))
			return;
	}

	CONST DWORD dwNewBrightness = CLAMP((INT)dwCurrentBrightness + iIncOrDecrement, (INT)dwMinimumBrightness, (INT)dwMaximumBrightness);
	if (LIKELY(dwNewBrightness != dwCurrentBrightness))
		SetMonitorBrightness(lpPhysicalMonitors[0].hPhysicalMonitor, dwNewBrightness);
}

VOID PrimaryDisplayOff(VOID)
{
	if (!cPhysicalMonitors) {
		PopulatePhysicalPrimaryMonitor();
		if (UNLIKELY(!cPhysicalMonitors))
			return;
	}

	for (DWORD i = 0; i < 3; ++i) {
		if (!SetVCPFeature(lpPhysicalMonitors[0].hPhysicalMonitor, 0xD6, 0x05)) // HardOff
			break;
	}
}
