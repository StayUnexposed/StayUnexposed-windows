#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace su {

using json = nlohmann::json;

// One leaked stealer-log record.
struct LeakRow {
  std::string id;            // empty when the plan doesn't return IDs
  std::string url;           // stealer logs only
  std::string username;      // full or masked, whatever the API returned
  std::string password;      // only present for records already unlocked on the account
  std::optional<int> strength;
  bool unlocked = false;
  std::string addedAt;       // ISO date-time
  std::string info;          // category (domain) or dataset label
  std::optional<bool> isEmail;
};

struct LeakPage {
  std::vector<LeakRow> items;
  int64_t total = 0;
  int64_t totalUnlocked = 0;
  int page = 1;
  int pageSize = 100;
  bool blacklisted = false;  // the searched value is protected and can't be searched
};

struct DomainSummary {
  int64_t employees = 0;
  int64_t customers = 0;
  int64_t thirdParties = 0;
  bool approximate = false;
  bool blacklisted = false;
};

struct Usage {
  std::optional<int64_t> used;
  std::optional<int64_t> limit;
  std::optional<int64_t> resetEpoch;
};

// "Both / email only / username only" identifier filter.
enum class IdKind { Both = 0, Email = 1, Username = 2 };

enum class DomainCategory { Employees = 0, Customers = 1, ThirdParties = 2, All = 3 };

struct Profile {
  std::string firstName;
  std::string lastName;
  std::string email;
  std::string planName;
  std::optional<int64_t> points;      // subscription + extra points
  std::optional<int64_t> quotaBytes;  // raw download quota left (subscription + extra)
  bool bypassPoints = false;          // every record readable without unlocking
  bool subscriptionActive = false;
};

struct LiveStats {
  int64_t rawLinesTotal = 0;
  int64_t rawLines7d = 0;
  int64_t leaksTotal = 0;
  int64_t leaks7d = 0;
  int64_t darkWebPosts = 0;
  double linesPerSecond24h = 0.0;
  int64_t sourcesActive = 0;
  std::vector<int64_t> sparkline24h;  // lines added per hour, oldest first (24 values)
  bool available = false;
};

// Size and growth of one dataset (GET /stats).
struct DatasetStats {
  int64_t total = 0, today = 0, thisWeek = 0, thisMonth = 0;
  std::vector<std::string> weeks;  // first day of each week (YYYY-MM-DD), oldest first
  std::vector<int64_t> perWeek;    // running total at each week
  bool present = false;
};

struct IndexStats {
  DatasetStats leaks, rawLines, combolists;
  int64_t darkWebPosts = 0;
  bool available = false;
};

struct DarkWebPost {
  std::string id;
  std::string title;
  std::string content;
  std::string source;
  std::string author;
  std::string target;
  std::string sourceRef;
  std::string publishedAt;
  std::string ingestedAt;
  bool truncated = false;
  bool censored = false;
};

struct DarkWebPage {
  std::vector<DarkWebPost> items;
  int64_t total = 0;
  int page = 1;
  int pageSize = 25;
};

// ---------------- Advanced search ----------------
// One include/exclude filter per field of the API's LeakSearchFilters, in the order shown on screen.
enum class AdvField {
  Url, UrlHost, UrlDomain, UrlScheme, UrlTld, UrlPort,      // URL & domain
  Username, Password, UsernameHash, PasswordHash,          // credentials
  EmailHost, EmailDomain, EmailTld,                        // email breakdown
  Count
};
enum class MatchType { Contains = 0, StartsWith = 1, EndsWith = 2 };

struct AdvFieldInfo {
  const char* key;        // API name, e.g. "url_domain" (exclude = key + "_not")
  const char* label;      // "URL domain"
  const char* help;       // tooltip
  const char* includeHint;
  const char* excludeHint;  // nullptr = include only
  bool matchType;         // has contains / starts with / ends with
  bool numeric;           // values are integers (ports)
  int minLen, maxLen;     // API limits per value
};
const AdvFieldInfo& advFieldInfo(AdvField f);

struct AdvFilter {
  std::vector<std::string> include, exclude;
  MatchType includeMatch = MatchType::Contains;
  MatchType excludeMatch = MatchType::Contains;
};

struct AdvQuery {
  AdvFilter fields[static_cast<int>(AdvField::Count)];
  IdKind idKind = IdKind::Both;
  int strength = 0;          // 0 any, 1 too_weak, 2 weak, 3 medium, 4 strong
  std::string addedFrom;     // YYYY-MM-DD (UTC), optional
  std::string addedTo;       // YYYY-MM-DD (UTC), optional
  bool forceAnd = false;

  AdvFilter& operator[](AdvField f) { return fields[static_cast<int>(f)]; }
  const AdvFilter& operator[](AdvField f) const { return fields[static_cast<int>(f)]; }
  int valueCount() const;  // include + exclude values over all fields
  bool hasFilters() const { return valueCount() > 0; }
};

inline constexpr int kAdvMaxValuesPerField = 50;
// Empty string when `value` is acceptable for the field, otherwise a short reason.
std::string advValueError(AdvField f, const std::string& value);
// "YYYY-MM-DD" with a real month/day.
bool isIsoDate(const std::string& s);
const char* strengthLabel(std::optional<int> score);

// ---------------- Parsing / building ----------------
LeakRow parseStealerRow(const json& j);
LeakPage parseLeakPage(const json& j);
DomainSummary parseDomainSummary(const json& j);
Usage parseUsage(const json& j);
Profile parseProfile(const json& j);
LiveStats parseLiveStats(const json& j);
IndexStats parseIndexStats(const json& j);
DarkWebPage parseDarkWebPage(const json& j);
json buildAdvancedBody(const AdvQuery& q);

// Human-friendly numbers: 831632049166 -> "831,632,049,166"; compact: 196000 -> "196K", 5.8e9 -> "5.8B"
std::string formatThousands(int64_t v);
std::string formatCompact(double v);
std::string formatBytes(int64_t bytes);

// Stable identity of a record, used to detect new leaks between daily checks.
std::string leakKey(const LeakRow& r);

}  // namespace su
