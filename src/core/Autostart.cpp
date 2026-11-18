#include "core/Autostart.h"

#include <windows.h>

#include <iterator>
#include <string>

#include "AppInfo.h"

namespace su::autostart {

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool isEnabled() {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
  LSTATUS st = RegQueryValueExW(key, APP_NAME_W, nullptr, nullptr, nullptr, nullptr);
  RegCloseKey(key);
  return st == ERROR_SUCCESS;
}

bool setEnabled(bool enabled) {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
  LSTATUS st;
  if (enabled) {
    wchar_t exe[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --minimized";
    st = RegSetValueExW(key, APP_NAME_W, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                        static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    st = RegDeleteValueW(key, APP_NAME_W);
    if (st == ERROR_FILE_NOT_FOUND) st = ERROR_SUCCESS;
  }
  RegCloseKey(key);
  return st == ERROR_SUCCESS;
}

void migrateLegacy() {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
  const bool hadLegacy = RegQueryValueExW(key, APP_LEGACY_NAME_W, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
  if (hadLegacy) RegDeleteValueW(key, APP_LEGACY_NAME_W);
  RegCloseKey(key);
  if (hadLegacy) setEnabled(true);
}

}  // namespace su::autostart
