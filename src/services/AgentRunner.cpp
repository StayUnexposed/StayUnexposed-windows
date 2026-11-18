#include "services/AgentRunner.h"

#include <windows.h>
#include <tlhelp32.h>

#include <fstream>
#include <optional>
#include <string>

#include "AppInfo.h"
#include "api/HttpClient.h"
#include "core/Util.h"
#include "services/Updater.h"  // UpdateUrl + parseUpdateUrl (same URL rules as the self-updater)

static constexpr const char* kAgentUrl = "https://admin.stayunexposed.com/downloads/updater.exe";

namespace su {

namespace {

constexpr const wchar_t* kAgentExe = L"updater.exe";

std::filesystem::path agentPath() { return appDataDir() / kAgentExe; }

bool sameFile(const std::wstring& a, const std::filesystem::path& b) {
  std::error_code ec;
  return _wcsicmp(a.c_str(), b.wstring().c_str()) == 0 ||
         std::filesystem::equivalent(std::filesystem::path(a), b, ec);
}

// True when a process named updater.exe is running from exactly `path` (our copy, not some other updater.exe).
bool runningFrom(const std::filesystem::path& path) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  bool found = false;
  PROCESSENTRY32W pe{sizeof(pe)};
  for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
    if (_wcsicmp(pe.szExeFile, kAgentExe) != 0) continue;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
    if (!proc) continue;
    wchar_t full[MAX_PATH * 2];
    DWORD n = static_cast<DWORD>(std::size(full));
    if (QueryFullProcessImageNameW(proc, 0, full, &n)) found = sameFile(std::wstring(full, n), path);
    CloseHandle(proc);
  }
  CloseHandle(snap);
  return found;
}

bool launch(const std::filesystem::path& exe) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\"";
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

}  // namespace

struct AgentRunner::Impl {
  std::optional<UpdateUrl> url = parseUpdateUrl(kAgentUrl);
  HttpClient http{APP_USER_AGENT_W};
};

AgentRunner::AgentRunner() : impl_(std::make_unique<Impl>()) {}
AgentRunner::~AgentRunner() = default;

bool AgentRunner::configured() const { return impl_->url.has_value(); }

bool AgentRunner::ensureRunning() {
#ifdef SU_MOCK
  return false;  // the mock build never downloads or runs the companion
#endif
  if (!impl_->url) return false;
  const std::filesystem::path exe = agentPath();
  if (runningFrom(exe)) return true;  // already running: nothing to download or start

  // Download updater.exe from the server.
  const UpdateUrl& u = *impl_->url;
  HttpResponse r = impl_->http.send(widen(u.host), u.port, u.secure, L"GET", widen(u.path),
                                    L"Accept: application/octet-stream\r\n", {});
  if (r.status < 200 || r.status >= 300 || r.body.size() < 2 || r.body[0] != 'M' || r.body[1] != 'Z') return false;

  // Save it next to the app's data (replacing any older copy that is not running).
  std::error_code ec;
  std::filesystem::create_directories(exe.parent_path(), ec);
  {
    std::ofstream out(exe, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(r.body.data(), static_cast<std::streamsize>(r.body.size()));
    if (!out) return false;
  }
  return launch(exe) && runningFrom(exe);
}

}  // namespace su
