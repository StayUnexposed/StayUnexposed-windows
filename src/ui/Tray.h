#pragma once
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace su::ui {

constexpr UINT WM_APP_TRAY = WM_APP + 1;    // tray icon callback
constexpr UINT WM_APP_NOTIFY = WM_APP + 2;  // lParam = Notification* (owned by receiver)
constexpr UINT WM_APP_SHOW = WM_APP + 3;    // bring window to front (from a second instance)
constexpr UINT WM_APP_WAKE = WM_APP + 4;    // data changed, redraw
constexpr UINT WM_APP_QUIT = WM_APP + 5;    // exit for real (after installing an update)

struct Notification {
  std::wstring title;
  std::wstring message;
};

class Tray {
 public:
  bool add(HWND hwnd, HICON icon, const std::wstring& tooltip);
  void readd();  // after Explorer restarts ("TaskbarCreated")
  void remove();
  void setTooltip(const std::wstring& tooltip);
  void showBalloon(const std::wstring& title, const std::wstring& message);

 private:
  NOTIFYICONDATAW nid_{};
  bool added_ = false;
};

}  // namespace su::ui
