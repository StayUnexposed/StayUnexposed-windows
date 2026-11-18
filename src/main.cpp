// Application entry point: Win32 window + Direct3D 11 + Dear ImGui, system tray, single instance.
#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <memory>
#include <string>

#include "../res/resource.h"
#include "AppInfo.h"
#include "api/ApiClient.h"
#include "core/Autostart.h"
#include "core/Database.h"
#include "core/Util.h"
#include "services/Updater.h"
#include "services/WatchService.h"
#include "ui/App.h"
#include "ui/Style.h"
#include "ui/Tray.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

const wchar_t* kWindowClass = L"StayUnexposed.MainWindow";
const wchar_t* kInstanceMutex = L"Local\\StayUnexposed.SingleInstance";

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
bool g_occluded = false;
UINT g_resizeW = 0, g_resizeH = 0;
UINT g_taskbarCreated = 0;
bool g_quitting = false;

su::Database* g_db = nullptr;
su::ui::App* g_app = nullptr;
su::ui::Tray g_tray;
su::WatchService* g_watch = nullptr;

void createRenderTarget() {
  ID3D11Texture2D* back = nullptr;
  g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
  if (back) {
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
  }
}

void cleanupRenderTarget() {
  if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool createDevice(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                             D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
  if (hr == DXGI_ERROR_UNSUPPORTED)  // no GPU: software rasterizer
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                       &g_swapChain, &g_device, &got, &g_context);
  if (FAILED(hr)) return false;
  createRenderTarget();
  return true;
}

void cleanupDevice() {
  cleanupRenderTarget();
  if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
  if (g_context) { g_context->Release(); g_context = nullptr; }
  if (g_device) { g_device->Release(); g_device = nullptr; }
}

void showMainWindow(HWND hwnd) {
  if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
  else ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);
}

void showTrayMenu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1, L"Open");
  AppendMenuW(menu, MF_STRING | (g_watch && g_watch->busy() ? MF_GRAYED : 0), 2, L"Check watchlist now");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 3, L"Exit");
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);
  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
  switch (cmd) {
    case 1: showMainWindow(hwnd); break;
    case 2: if (g_watch) g_watch->requestCheckAll(); break;
    case 3: g_quitting = true; DestroyWindow(hwnd); break;
    default: break;
  }
}

LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

  if (msg == g_taskbarCreated && g_taskbarCreated) {
    g_tray.readd();
    return 0;
  }

  switch (msg) {
    case WM_SIZE:
      if (wParam != SIZE_MINIMIZED) {
        g_resizeW = LOWORD(lParam);
        g_resizeH = HIWORD(lParam);
      }
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
      mmi->ptMinTrackSize = {900, 560};
      return 0;
    }
    case WM_SYSCOMMAND:
      if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // disable ALT application menu
      break;
    case WM_CLOSE:
      if (!g_quitting && g_db && g_db->getSetting(su::kSettingMinimizeToTray, "1") == "1") {
        ShowWindow(hwnd, SW_HIDE);  // keep monitoring in the background
        return 0;
      }
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case su::ui::WM_APP_TRAY:
      switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK: showMainWindow(hwnd); break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: showTrayMenu(hwnd); break;
        case NIN_BALLOONUSERCLICK:
          if (g_app) g_app->showMonitoring();
          showMainWindow(hwnd);
          break;
        default: break;
      }
      return 0;
    case su::ui::WM_APP_NOTIFY: {
      std::unique_ptr<su::ui::Notification> n(reinterpret_cast<su::ui::Notification*>(lParam));
      g_tray.showBalloon(n->title, n->message);
      return 0;
    }
    case su::ui::WM_APP_SHOW:
      showMainWindow(hwnd);
      return 0;
    case su::ui::WM_APP_WAKE:
      return 0;
    case su::ui::WM_APP_QUIT:
      g_quitting = true;
      DestroyWindow(hwnd);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
  // ---- Single instance: forward to the running copy ----
  HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
  const bool justUpdated = cmdLine && wcsstr(cmdLine, L"--updated") != nullptr;
  if (GetLastError() == ERROR_ALREADY_EXISTS && justUpdated && mutex) {
    // Started by the previous version after an update: wait for it to finish closing.
    const DWORD w = WaitForSingleObject(mutex, 15000);
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) SetLastError(0);
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(kWindowClass, nullptr)) PostMessageW(other, su::ui::WM_APP_SHOW, 0, 0);
    if (mutex) CloseHandle(mutex);
    return 0;
  }
  su::autostart::migrateLegacy();
  su::Updater::cleanupPrevious();
  const bool startMinimized = cmdLine && wcsstr(cmdLine, L"--minimized") != nullptr;

  ImGui_ImplWin32_EnableDpiAwareness();

  // ---- Data + services ----
  su::Database db;
  if (!db.open(su::appDataDir() / L"watch.db")) {
    MessageBoxW(nullptr, su::widen("Cannot open the local database:\n" + db.lastError()).c_str(), APP_NAME_W,
                MB_ICONERROR);
    return 1;
  }
  g_db = &db;
  // Intentionally leaked: detached search threads may still use it while the process exits.
  auto* api = new su::ApiClient();

  // ---- Window ----
  HICON bigIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
  HICON smallIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
  WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wndProc, 0, 0, instance, bigIcon, LoadCursorW(nullptr, IDC_ARROW),
                 nullptr, nullptr, kWindowClass, smallIcon};
  RegisterClassExW(&wc);

  POINT origin{0, 0};
  HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
  const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(monitor);
  int w = static_cast<int>(1280 * scale), h = static_cast<int>(800 * scale);
  HWND hwnd = CreateWindowExW(0, kWindowClass, APP_NAME_W, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                              nullptr, nullptr, instance, nullptr);
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

  if (!createDevice(hwnd)) {
    cleanupDevice();
    MessageBoxW(nullptr, L"Direct3D 11 could not be initialised.", APP_NAME_W, MB_ICONERROR);
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  su::ui::setupStyle(scale);
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_device, g_context);

  // ---- App + background monitoring ----
  su::WatchService watch(
      db, su::WatchService::apiFetcher(*api),
      [hwnd](const std::string& title, const std::string& message) {
        auto* n = new su::ui::Notification{su::widen(title), su::widen(message)};
        if (!PostMessageW(hwnd, su::ui::WM_APP_NOTIFY, 0, reinterpret_cast<LPARAM>(n))) delete n;
      },
      [hwnd] {
        if (g_app) g_app->markDirty();
        PostMessageW(hwnd, su::ui::WM_APP_WAKE, 0, 0);
      });
  g_watch = &watch;

  su::ui::AppContext ctx{db, *api, watch, {}};
  ctx.quit = [hwnd] { PostMessageW(hwnd, su::ui::WM_APP_QUIT, 0, 0); };
  su::ui::App app(ctx);
  g_app = &app;
#ifdef SU_MOCK
  app.loadSampleData();  // fill every page with sample data straight away
#endif

  g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  g_tray.add(hwnd, smallIcon, APP_NAME_W);
  watch.start();

  if (!startMinimized) {
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
  }

  // ---- Main loop ----
  const ImVec4 clear(0.09f, 0.10f, 0.12f, 1.0f);
  bool done = false;
  while (!done) {
    const bool visible = IsWindowVisible(hwnd) && !IsIconic(hwnd);
    if (!visible || g_occluded) {
      // Nothing to draw: sleep until a message arrives (tray, notifications, show).
      MsgWaitForMultipleObjects(0, nullptr, FALSE, g_occluded ? 100 : 1000, QS_ALLINPUT);
    } else if (GetForegroundWindow() != hwnd) {
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);  // ~20 fps in background
    }

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (msg.message == WM_QUIT) done = true;
    }
    if (done) break;

    app.tick();  // update checks, companion updater, referral check-ins — even while hidden in the tray

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) continue;
    if (g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) continue;
    g_occluded = false;

    if (g_resizeW != 0 && g_resizeH != 0) {
      cleanupRenderTarget();
      g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
      g_resizeW = g_resizeH = 0;
      createRenderTarget();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    app.render();
    ImGui::Render();

    const float c[4] = {clear.x, clear.y, clear.z, clear.w};
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, c);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = g_swapChain->Present(1, 0);
    g_occluded = hr == DXGI_STATUS_OCCLUDED;
  }

  // ---- Shutdown ----
  api->cancelAll();
  watch.stop();
  g_app = nullptr;
  g_watch = nullptr;
  g_tray.remove();

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  cleanupDevice();
  if (IsWindow(hwnd)) DestroyWindow(hwnd);
  UnregisterClassW(kWindowClass, instance);
  g_db = nullptr;
  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  return 0;
}
