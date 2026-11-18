#pragma once
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "api/Models.h"

struct sqlite3;

namespace su {

enum class WatchKind { Email = 0, Username = 1, Domain = 2 };
// Combolist = 1 is only found in data saved by older versions (combolists are no longer checked).
enum class LeakSource { Stealer = 0, Combolist = 1, Domain = 2 };

const char* watchKindLabel(WatchKind k);
const char* leakSourceLabel(LeakSource s);

constexpr int64_t kTotalUnknown = -1;      // never checked successfully
constexpr int64_t kTotalUnavailable = -2;  // dataset not available on this plan

struct WatchItem {
  int64_t id = 0;
  std::string value;
  WatchKind kind = WatchKind::Email;
  int64_t createdAt = 0;
  int64_t lastCheckAt = 0;
  int64_t stealerTotal = kTotalUnknown;  // stealer-log total (for a domain: all domain leaks)
  int64_t comboTotal = kTotalUnknown;
  std::string lastStatus;     // "" = OK, otherwise a short error text
  bool baselineDone = false;
  int64_t unreadNew = 0;      // new leaks in unread alerts
};

struct AlertItem {
  int64_t id = 0;
  int64_t watchId = 0;
  std::string watchValue;
  int64_t createdAt = 0;
  int64_t newCount = 0;
  std::string sources;        // e.g. "Stealer logs, Combolists"
  bool read = false;
};

// Thread-safe wrapper around the local SQLite database.
class Database {
 public:
  Database() = default;
  ~Database();
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  bool open(const std::filesystem::path& file);  // ":memory:" supported via path(":memory:")
  const std::string& lastError() const { return lastError_; }

  std::string getSetting(const std::string& key, const std::string& fallback = {});
  void setSetting(const std::string& key, const std::string& value);

  // Returns the new id, or 0 if the value is already watched.
  int64_t addWatch(const std::string& value, WatchKind kind);
  void removeWatch(int64_t id);
  std::vector<WatchItem> listWatches();
  bool getWatch(int64_t id, WatchItem& out);
  void updateWatchResult(int64_t id, int64_t checkedAt, int64_t stealerTotal, int64_t comboTotal,
                         const std::string& status, bool baselineDone);

  std::unordered_set<std::string> seenKeys(int64_t watchId, LeakSource source);
  void insertSeen(int64_t watchId, LeakSource source, const std::vector<LeakRow>& rows, int64_t now,
                  int64_t alertId);
  std::vector<LeakRow> watchLeaks(int64_t watchId);  // all known leaks, newest first

  int64_t createAlert(int64_t watchId, int64_t now, int64_t newCount, const std::string& sources);
  std::vector<AlertItem> listAlerts(int limit = 500);
  std::vector<LeakRow> alertLeaks(int64_t alertId);
  void markAlertRead(int64_t alertId);
  void markAllAlertsRead();
  int64_t unreadAlertCount();

 private:
  bool exec(const char* sql);

  sqlite3* db_ = nullptr;
  std::recursive_mutex mutex_;
  std::string lastError_;
};

}  // namespace su
