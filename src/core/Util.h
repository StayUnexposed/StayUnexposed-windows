#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace su {

std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);

int64_t nowEpoch();
std::string formatLocalTime(int64_t epoch);   // "2026-11-14 08:00", "-" for 0
std::string localDate(int64_t epoch);         // "2026-11-14"
int localMinutesOfDay(int64_t epoch);         // 0..1439
bool parseHHMM(std::string_view s, int& minutesOut);
std::string formatHHMM(int minutes);

// "2026-10-02T13:45:00Z" / "2026-10-02T13:45:00.123+00:00" -> "2026-10-02"
std::string shortDate(std::string_view isoDateTime);

std::string urlEncode(std::string_view s);
std::string trim(std::string_view s);
std::vector<std::string> splitList(std::string_view s, char sep = ',');  // trimmed, no empties
bool looksLikeEmail(std::string_view s);
// "https://Example.COM/login" -> "example.com": lowercase host, no scheme, port, path or trailing dot.
std::string normalizeDomain(std::string_view s);
// True for host names like "example.com" or "mail.example.co.uk" (letters/digits/hyphens, an alphabetic TLD).
bool looksLikeDomain(std::string_view s);

// %APPDATA%\<APP_NAME>, created on demand.
std::filesystem::path appDataDir();

// Opens an http(s) URL in the default browser. Bare hosts ("site.com/login") get https://.
// Other schemes (android://, file://, ...) are refused. Returns false if nothing was opened.
bool openUrl(const std::string& url);
// Opens Explorer with the file selected.
void revealInExplorer(const std::filesystem::path& file);

// Interruptible sleep: returns false if `cancelled` became true while waiting.
bool sleepFor(int milliseconds, const std::atomic<bool>* cancelled = nullptr);

}  // namespace su
