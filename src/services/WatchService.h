#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "api/ApiClient.h"
#include "core/Database.h"

namespace su {

// Settings keys
inline constexpr const char* kSettingCheckTime = "check_time";       // "HH:MM", default 08:00
inline constexpr const char* kSettingLastDailyRun = "last_daily_run";  // epoch seconds
inline constexpr const char* kSettingMinimizeToTray = "minimize_to_tray";

// Daily monitoring of saved emails / usernames.
//  - Adding a value records a baseline (no alert).
//  - Once per day at the configured time (or on start-up if a run was missed) every value is
//    re-checked; leaks whose key was never seen before raise an alert.
class WatchService {
 public:
  using Fetcher = std::function<Result<LeakPage>(const WatchItem&, LeakSource, int page)>;
  using NotifyFn = std::function<void(const std::string& title, const std::string& message)>;
  using ChangedFn = std::function<void()>;

  static constexpr int kPageSize = 100;
  static constexpr int kDomainPageSize = 1000;
  static constexpr int64_t kMaxRecords = 5000;  // per value and dataset

  // Datasets checked for each kind of watched value.
  static std::vector<LeakSource> sourcesFor(WatchKind kind);

  WatchService(Database& db, Fetcher fetcher, NotifyFn notify, ChangedFn changed);
  ~WatchService();

  // Default fetcher backed by the real API.
  static Fetcher apiFetcher(ApiClient& api);

  void start();
  void stop();

  void requestCheck(int64_t watchId);  // queue one value
  void requestCheckAll();              // queue every value now

  bool busy() const { return busy_; }
  std::string progress() const;

  // Synchronous check of one item (used by the worker thread and by tests).
  void checkItem(int64_t watchId);

  // True when the daily run is due at `now` given the configured time and the last run.
  static bool dailyDue(int64_t now, int checkMinutes, int64_t lastRun);

 private:
  void threadMain();
  void runQueue();
  void setProgress(const std::string& s);
  int pauseMs() const { return pauseBetweenRequestsMs_; }

  Database& db_;
  Fetcher fetcher_;
  NotifyFn notify_;
  ChangedFn changed_;

  std::thread thread_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<int64_t> queue_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> busy_{false};
  std::string progress_;

 public:
  int pauseBetweenRequestsMs_ = 400;  // politeness delay; tests set 0
};

}  // namespace su
