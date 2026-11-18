#include "ui/Tray.h"

#include <cwchar>
#include <iterator>

namespace su::ui {

static void copyTruncated(wchar_t* dst, size_t cap, const std::wstring& src) {
  wcsncpy_s(dst, cap, src.c_str(), _TRUNCATE);
}

bool Tray::add(HWND hwnd, HICON icon, const std::wstring& tooltip) {
  nid_ = {};
  nid_.cbSize = sizeof(nid_);
  nid_.hWnd = hwnd;
  nid_.uID = 1;
  nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid_.uCallbackMessage = WM_APP_TRAY;
  nid_.hIcon = icon;
  copyTruncated(nid_.szTip, std::size(nid_.szTip), tooltip);
  added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
  if (added_) {
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
  }
  return added_;
}

void Tray::readd() {
  if (!nid_.hWnd) return;
  NOTIFYICONDATAW copy = nid_;
  copy.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  added_ = Shell_NotifyIconW(NIM_ADD, &copy) != FALSE;
  if (added_) {
    copy.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &copy);
  }
}

void Tray::remove() {
  if (added_) Shell_NotifyIconW(NIM_DELETE, &nid_);
  added_ = false;
}

void Tray::setTooltip(const std::wstring& tooltip) {
  if (!added_) return;
  if (wcscmp(nid_.szTip, tooltip.c_str()) == 0) return;
  copyTruncated(nid_.szTip, std::size(nid_.szTip), tooltip);
  nid_.uFlags = NIF_TIP | NIF_SHOWTIP;
  Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void Tray::showBalloon(const std::wstring& title, const std::wstring& message) {
  if (!added_) return;
  NOTIFYICONDATAW n = nid_;
  n.uFlags = NIF_INFO;
  n.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
  copyTruncated(n.szInfoTitle, std::size(n.szInfoTitle), title);
  copyTruncated(n.szInfo, std::size(n.szInfo), message);
  Shell_NotifyIconW(NIM_MODIFY, &n);
}

}  // namespace su::ui
