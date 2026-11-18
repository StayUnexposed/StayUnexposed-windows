#include "core/Util.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cctype>
#include <chrono>
#include <ctime>
#include <thread>

#include "AppInfo.h"

namespace su {

std::wstring widen(std::string_view s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

std::string narrow(std::wstring_view s) {
  if (s.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

int64_t nowEpoch() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

static std::tm toLocal(int64_t epoch) {
  std::tm tm{};
  time_t t = static_cast<time_t>(epoch);
  localtime_s(&tm, &t);
  return tm;
}

std::string formatLocalTime(int64_t epoch) {
  if (epoch <= 0) return "-";
  std::tm tm = toLocal(epoch);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
  return buf;
}

std::string localDate(int64_t epoch) {
  if (epoch <= 0) return {};
  std::tm tm = toLocal(epoch);
  char buf[16];
  std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
  return buf;
}

int localMinutesOfDay(int64_t epoch) {
  std::tm tm = toLocal(epoch);
  return tm.tm_hour * 60 + tm.tm_min;
}

bool parseHHMM(std::string_view s, int& minutesOut) {
  std::string t = trim(s);
  int h = 0, m = 0;
  char extra = 0;
  if (sscanf_s(t.c_str(), "%d:%d%c", &h, &m, &extra, 1) != 2) return false;
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  minutesOut = h * 60 + m;
  return true;
}

std::string formatHHMM(int minutes) {
  char buf[8];
  snprintf(buf, sizeof buf, "%02d:%02d", (minutes / 60) % 24, minutes % 60);
  return buf;
}

std::string shortDate(std::string_view iso) {
  if (iso.size() >= 10) return std::string(iso.substr(0, 10));
  return std::string(iso);
}

std::string urlEncode(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 15]);
    }
  }
  return out;
}

std::string trim(std::string_view s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

std::vector<std::string> splitList(std::string_view s, char sep) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t pos = s.find(sep, start);
    if (pos == std::string_view::npos) pos = s.size();
    std::string item = trim(s.substr(start, pos - start));
    if (!item.empty()) out.push_back(std::move(item));
    start = pos + 1;
  }
  return out;
}

bool looksLikeEmail(std::string_view s) {
  auto at = s.find('@');
  return at != std::string_view::npos && at > 0 && at + 1 < s.size();
}

std::string normalizeDomain(std::string_view s) {
  std::string v = trim(s);
  for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (auto scheme = v.find("://"); scheme != std::string::npos) v.erase(0, scheme + 3);
  if (auto cut = v.find_first_of("/?#"); cut != std::string::npos) v.erase(cut);
  if (auto colon = v.find(':'); colon != std::string::npos) v.erase(colon);
  while (!v.empty() && v.back() == '.') v.pop_back();
  return v;
}

bool looksLikeDomain(std::string_view s) {
  if (s.empty() || s.size() > 253 || s.find('@') != std::string_view::npos) return false;
  size_t labelStart = 0, labels = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i < s.size() && s[i] != '.') {
      const unsigned char c = static_cast<unsigned char>(s[i]);
      if (!std::isalnum(c) && c != '-') return false;
      continue;
    }
    std::string_view label = s.substr(labelStart, i - labelStart);
    if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
    ++labels;
    if (i == s.size()) {  // top-level domain: letters only, 2+ characters
      if (label.size() < 2) return false;
      for (char c : label)
        if (!std::isalpha(static_cast<unsigned char>(c))) return false;
    }
    labelStart = i + 1;
  }
  return labels >= 2;
}

std::filesystem::path appDataDir() {
  PWSTR raw = nullptr;
  std::filesystem::path dir;
  const std::filesystem::path base =
      SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)) ? std::filesystem::path(raw)
                                                                                 : std::filesystem::current_path();
  CoTaskMemFree(raw);
  dir = base / APP_NAME_W;
  std::error_code ec;
  // Earlier builds used another name: carry the watchlist, alerts and settings over once.
  const std::filesystem::path legacy = base / APP_LEGACY_NAME_W;
  if (!std::filesystem::exists(dir, ec) && std::filesystem::is_directory(legacy, ec))
    std::filesystem::rename(legacy, dir, ec);  // on failure a fresh folder is created below
  std::filesystem::create_directories(dir, ec);
  return dir;
}

bool openUrl(const std::string& raw) {
  std::string url = trim(raw);
  if (url.empty()) return false;
  std::string lower = url;
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lower.find("://") == std::string::npos) url = "https://" + url;
  else if (lower.rfind("http://", 0) != 0 && lower.rfind("https://", 0) != 0) return false;
  for (char c : url)
    if (static_cast<unsigned char>(c) < 0x20 || c == ' ' || c == '"') return false;
  auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return r > 32;
}

void revealInExplorer(const std::filesystem::path& file) {
  std::wstring args = L"/select,\"" + file.wstring() + L"\"";
  ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

bool sleepFor(int ms, const std::atomic<bool>* cancelled) {
  const int step = 100;
  for (int waited = 0; waited < ms; waited += step) {
    if (cancelled && *cancelled) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(ms - waited < step ? ms - waited : step));
  }
  return !(cancelled && *cancelled);
}

}  // namespace su
