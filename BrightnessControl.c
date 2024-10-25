#include "stdafx.h"

static DWORD cPhysicalMonitors = 0;
static LPPHYSICAL_MONITOR lpPhysicalMonitors = NULL;

VOID ClearPhysicalMonitors(VOID)
{
	if (lpPhysicalMonitors) {
		if (cPhysicalMonitors)
			DestroyPhysicalMonitors(cPhysicalMonitors, lpPhysicalMonitors);
		HeapFree(GetProcessHeap(), 0, lpPhysicalMonitors);
		lpPhysicalMonitors = NULL;
	}

	cPhysicalMonitors = 0;
}

static VOID PopulatePhysicalPrimaryMonitor(VOID)
{
	if (lpPhysicalMonitors)
		return;

	CONST HMONITOR hMonitor = MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY);
	if (!hMonitor)
		return;

	if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &cPhysicalMonitors) || !cPhysicalMonitors)
		return;

	lpPhysicalMonitors = (LPPHYSICAL_MONITOR)HeapAlloc(GetProcessHeap(), 0, sizeof(PHYSICAL_MONITOR) * cPhysicalMonitors);
	if (!lpPhysicalMonitors)
		return;

	if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, cPhysicalMonitors, lpPhysicalMonitors)) {
		cPhysicalMonitors = 0;
		ClearPhysicalMonitors();
	}
}

VOID SetBrightness(INT iIncOrDecrement)
{
	DWORD dwMinimumBrightness, dwCurrentBrightness, dwMaximumBrightness;

	if (!lpPhysicalMonitors || !GetMonitorBrightness(lpPhysicalMonitors[0].hPhysicalMonitor, &dwMinimumBrightness, &dwCurrentBrightness, &dwMaximumBrightness)) {
		ClearPhysicalMonitors();
		PopulatePhysicalPrimaryMonitor();
		if (!lpPhysicalMonitors || !GetMonitorBrightness(lpPhysicalMonitors[0].hPhysicalMonitor, &dwMinimumBrightness, &dwCurrentBrightness, &dwMaximumBrightness))
			return;
	}

	CONST DWORD dwNewBrightness = CLAMP((INT)dwCurrentBrightness + iIncOrDecrement, (INT)dwMinimumBrightness, (INT)dwMaximumBrightness);
	if (dwNewBrightness != dwCurrentBrightness)
		SetMonitorBrightness(lpPhysicalMonitors[0].hPhysicalMonitor, dwNewBrightness);
}