#include "services/Updater.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include "AppInfo.h"
#include "api/HttpClient.h"
#include "core/HardwareId.h"
#include "core/Signature.h"
#include "core/Util.h"

static constexpr const char* kUpdateApi = "https://admin.stayunexposed.com/api/app/latest";

namespace su {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string stripV(const std::string& v) { return !v.empty() && (v[0] == 'v' || v[0] == 'V') ? v.substr(1) : v; }

bool isVersion(const std::string& raw) {
  const std::string v = stripV(raw);
  if (v.empty() || v.size() > 32 || v.front() == '.' || v.back() == '.') return false;
  return v.find_first_not_of("0123456789.") == std::string::npos && v.find("..") == std::string::npos;
}

std::vector<int> versionParts(const std::string& v) {
  std::vector<int> parts;
  std::stringstream ss(stripV(v));
  std::string item;
  while (std::getline(ss, item, '.')) parts.push_back(std::atoi(item.c_str()));
  return parts;
}

fs::path currentExe() {
  wchar_t buf[MAX_PATH * 2] = {};
  GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
  return fs::path(buf);
}

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Last path segment of a URL, without query / fragment.
std::string urlFileName(const std::string& url) {
  std::string p = url.substr(0, url.find_first_of("?#"));
  const auto slash = p.find_last_of('/');
  return slash == std::string::npos ? p : p.substr(slash + 1);
}

bool endsWith(const std::string& s, const char* suffix) {
  const std::string x(suffix);
  return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

}  // namespace

bool UpdateUrl::sameServer(const UpdateUrl& o) const {
  auto norm = [](const std::string& h) { return h == "127.0.0.1" ? std::string("localhost") : h; };
  return secure == o.secure && norm(host) == norm(o.host) && port == o.port;
}

bool isInstallerUrl(const std::string& url) {
  const std::string name = lower(urlFileName(url));
  return name.find("setup") != std::string::npos || name.find("install") != std::string::npos || endsWith(name, ".msi");
}

std::optional<UpdateUrl> parseUpdateUrl(const std::string& url) {
  UpdateUrl u;
  std::string rest;
  if (url.rfind("https://", 0) == 0) {
    rest = url.substr(8);
  } else if (url.rfind("http://", 0) == 0) {
    u.secure = false;
    u.port = 80;
    rest = url.substr(7);
  } else {
    return std::nullopt;
  }
  const auto slash = rest.find('/');
  std::string authority = rest.substr(0, slash);
  u.path = slash == std::string::npos ? "/" : rest.substr(slash);
  if (authority.empty() || authority.find_first_of("@ \\") != std::string::npos) return std::nullopt;  // no credentials
  if (const auto colon = authority.find(':'); colon != std::string::npos) {
    const std::string port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5 || port.find_first_not_of("0123456789") != std::string::npos) return std::nullopt;
    const int p = std::atoi(port.c_str());
    if (p < 1 || p > 65535) return std::nullopt;
    u.port = static_cast<unsigned short>(p);
    authority.resize(colon);
  }
  for (auto& c : authority) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (authority.empty()) return std::nullopt;
  u.host = authority;
  // Plain HTTP can be tampered with on the network, so it is accepted only for a server on this PC.
  if (!u.secure && u.host != "localhost" && u.host != "127.0.0.1") return std::nullopt;
  return u;
}

int compareVersions(const std::string& a, const std::string& b) {
  const auto pa = versionParts(a), pb = versionParts(b);
  for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
    const int x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}

std::optional<UpdateInfo> parseUpdateManifest(const json& j, const UpdateUrl& api) {
  if (!j.is_object()) return std::nullopt;
  auto str = [&j](const char* k, const char* alt = nullptr) {
    for (const char* key : {k, alt}) {
      if (!key) continue;
      if (auto it = j.find(key); it != j.end() && it->is_string()) return it->get<std::string>();
    }
    return std::string();
  };
  UpdateInfo u{stripV(str("latestVersion", "version")), str("downloadUrl", "url"), str("sha256"), str("notes"),
               str("publishedAt")};
  for (auto& c : u.sha256) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (!isVersion(u.version)) return std::nullopt;
  const auto dl = parseUpdateUrl(u.url);
  if (!dl || !dl->sameServer(api)) return std::nullopt;
  if (!u.sha256.empty() &&
      (u.sha256.size() != 64 || u.sha256.find_first_not_of("0123456789abcdef") != std::string::npos))
    return std::nullopt;
  return u;
}

std::string updateCheckPath(const std::string& path, const std::string& version, const std::string& referralId,
                            bool agentActive) {
  const char sep = path.find('?') == std::string::npos ? '?' : '&';
  std::string out = path + sep + "version=" + urlEncode(version) + "&id=" + urlEncode(referralId);
  if (agentActive) out += "&updater=1";
  return out;
}

std::string formatReleaseDate(const std::string& iso) {
  if (iso.size() < 10 || iso[4] != '-' || iso[7] != '-') return {};
  const int y = std::atoi(iso.substr(0, 4).c_str()), m = std::atoi(iso.substr(5, 2).c_str()),
            d = std::atoi(iso.substr(8, 2).c_str());
  if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31) return {};
  static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return std::string(kMonths[m - 1]) + " " + std::to_string(d) + ", " + std::to_string(y);
}

Updater::Updater() : api_(parseUpdateUrl(kUpdateApi)), http_(std::make_unique<HttpClient>(APP_USER_AGENT_W)) {}

Updater::~Updater() = default;

Result<std::optional<UpdateInfo>> Updater::check(bool agentActive) {
  using R = Result<std::optional<UpdateInfo>>;
#ifdef SU_MOCK
  (void)agentActive;
  return R::success(std::nullopt);
#endif
  if (!api_) return R::success(std::nullopt);
  // The version and referral ID let the server note when this app was last seen (shown as active / inactive).
  const std::string path = updateCheckPath(api_->path, APP_VERSION_STR, hwid::userId(), agentActive);
  HttpResponse r = http_->send(widen(api_->host), api_->port, api_->secure, L"GET", widen(path),
                               L"Accept: application/json\r\n", {});
  if (r.status == 404) return R::success(std::nullopt);  // nothing published yet
  if (r.status < 200 || r.status >= 300) return R::failure({r.status, "Couldn't check for updates."});
  try {
    auto info = parseUpdateManifest(json::parse(r.body), *api_);
    if (!info) return R::failure({-2, "The update information is invalid."});
    if (compareVersions(info->version, APP_VERSION_STR) <= 0) return R::success(std::nullopt);
    return R::success(info);
  } catch (const json::exception&) {
    return R::failure({-2, "The update information is invalid."});
  }
}

Result<fs::path> Updater::download(const UpdateInfo& info) {
  using R = Result<fs::path>;
  const auto dl = parseUpdateUrl(info.url);
  if (!api_ || !dl || !dl->sameServer(*api_)) return R::failure({-2, "The update link is not trusted."});
  HttpResponse r = http_->send(widen(dl->host), dl->port, dl->secure, L"GET", widen(dl->path),
                               L"Accept: application/octet-stream\r\n", {});
  if (r.status < 200 || r.status >= 300 || r.body.empty())
    return R::failure({r.status, r.status == 0 ? "Can't reach the update server." : "The update download failed."});
  if (!info.sha256.empty() && hwid::sha256Hex(r.body) != info.sha256)
    return R::failure({-2, "The downloaded update is damaged (checksum mismatch). Nothing was changed."});
  const bool installer = isInstallerUrl(info.url);
  const std::string name = lower(urlFileName(info.url));
  const bool msi = installer && endsWith(name, ".msi");
  const bool isExe = r.body.size() >= 2 && r.body[0] == 'M' && r.body[1] == 'Z';
  const bool isMsi = r.body.size() >= 4 && static_cast<unsigned char>(r.body[0]) == 0xD0 &&
                     static_cast<unsigned char>(r.body[1]) == 0xCF && static_cast<unsigned char>(r.body[2]) == 0x11 &&
                     static_cast<unsigned char>(r.body[3]) == 0xE0;
  if (msi ? !isMsi : !isExe) return R::failure({-2, "The download is not a program."});

  fs::path target;
  if (installer) {
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / L"StayUnexposed-update";
    fs::create_directories(dir, ec);
    target = dir / (msi ? L"StayUnexposed-Setup.msi" : L"StayUnexposed-Setup.exe");  // fixed name: nothing from the URL
  } else {
    target = currentExe().replace_extension(".new.exe");
  }
  std::ofstream out(target, std::ios::binary | std::ios::trunc);
  if (!out) return R::failure({-3, "Can't save the update (no permission?)."});
  out.write(r.body.data(), static_cast<std::streamsize>(r.body.size()));
  out.close();
  if (!out) return R::failure({-3, "Can't save the update (disk full?)."});
  // Once this app is signed, only an update signed by the same publisher is installed.
  if (!signature::acceptableUpdateSigner(signature::ownSigner(), signature::verifiedSigner(target))) {
    std::error_code ec;
    fs::remove(target, ec);
    return R::failure({-2, "The update is not signed by Stay Unexposed. Nothing was changed."});
  }
  return R::success(target);
}

bool Updater::runInstaller(const fs::path& setup, std::string& error) {
  const bool msi = lower(setup.extension().string()) == ".msi";
  const std::wstring params = msi ? L"/i \"" + setup.wstring() + L"\"" : std::wstring();
  SHELLEXECUTEINFOW sei{sizeof(sei)};
  sei.fMask = SEE_MASK_NOASYNC;
  sei.lpVerb = L"open";  // honours the installer's UAC manifest (CreateProcess would fail for admin installers)
  sei.lpFile = msi ? L"msiexec.exe" : setup.c_str();
  sei.lpParameters = msi ? params.c_str() : nullptr;
  sei.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&sei)) {
    error = GetLastError() == ERROR_CANCELLED ? "The installer was not allowed to start." : "Can't start the installer.";
    return false;
  }
  return true;
}

bool Updater::install(const fs::path& newExe, std::string& error) {
  const fs::path exe = currentExe();
  const fs::path old = fs::path(exe).replace_extension(".old");
  DeleteFileW(old.c_str());
  if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    error = "Can't replace the app file (no permission?).";
    return false;
  }
  if (!MoveFileExW(newExe.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);  // put the current version back
    error = "Can't install the new version.";
    return false;
  }
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --updated";
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    MoveFileExW(exe.c_str(), newExe.c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
    error = "Can't start the new version.";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

void Updater::cleanupPrevious() {
  const fs::path exe = currentExe();
  DeleteFileW(fs::path(exe).replace_extension(".old").c_str());
  DeleteFileW(fs::path(exe).replace_extension(".new.exe").c_str());
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / L"StayUnexposed-update";
  if (!ec) fs::remove_all(dir, ec);  // installer from the previous update (fails harmlessly if still running)
}

}  // namespace su
