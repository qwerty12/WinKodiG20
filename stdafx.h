#pragma once

#if __GNUC__
#define GNUC_UNUSED __attribute__((__unused__))
#define LIKELY(exp) __builtin_expect(!!(exp), 1)
#define UNLIKELY(exp) __builtin_expect(!!(exp), 0)
#else
#define GNUC_UNUSED
#define LIKELY(exp) exp
#define UNLIKELY(exp) exp
#endif

// Windows Header Files
#include <winsdkver.h>
#define _WIN32_WINNT _WIN32_WINNT_MAXVER
#include <sdkddkver.h>
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define WIN32_EXTRA_LEAN
#include <windows.h>
#include <initguid.h>
#include <hidclass.h>
#include <hidsdi.h>
#include <cfgmgr32.h>
#include <Psapi.h>
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <powrprof.h>
// C RunTime Header Files
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#ifndef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

	VOID ClearPhysicalMonitors(VOID);
	VOID SetBrightness(INT);
	VOID PrimaryDisplayOff(VOID);

#ifdef __cplusplus
}
#endif
