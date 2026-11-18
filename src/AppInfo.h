#pragma once
// The ONLY place the product name/branding is defined. The version lives in VERSION (see Version.h).
// Plain #defines so the resource compiler (res/app.rc) can use them too.

#define APP_NAME            "Stay Unexposed"
#define APP_NAME_W          L"Stay Unexposed"
#define APP_COMPANY         "Stay Unexposed"
#define APP_DESCRIPTION     "Stay Unexposed"
#define APP_EXE_NAME        "StayUnexposed.exe"
#include "Version.h"  // APP_VERSION_STR / APP_VERSION_WSTR / APP_VERSION_NUM, generated from VERSION
#define APP_USER_AGENT_W    L"StayUnexposed/" APP_VERSION_WSTR

// Name used by earlier builds: existing data folder and start-with-Windows entry are migrated from it.
#define APP_LEGACY_NAME_W   L"BreachWatch"
