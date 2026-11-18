#include "services/WatchService.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_set>

#include "core/Util.h"

namespace su {

namespace {

struct SourceOutcome {
  bool attempted = false;
  bool ok = false;
  bool unavailable = false;  // 403: dataset not on this plan
  int64_t total = kTotalUnknown;
  std::vector<LeakRow> newRows;
  std::string error;
};

}  // namespace

WatchService::WatchService(Database& db, Fetcher fetcher, NotifyFn notify, ChangedFn changed)
    : db_(db), fetcher_(std::move(fetcher)), notify_(std::move(notify)), changed_(std::move(changed)) {}

WatchService::~WatchService() { stop(); }

WatchService::Fetcher WatchService::apiFetcher(ApiClient& api) {
  return [&api](const WatchItem& item, LeakSource source, int page) -> Result<LeakPage> {
    bool isUsername = item.kind == WatchKind::Username;
    switch (source) {
      case LeakSource::Domain:
        return api.domainLeaks(item.value, DomainCategory::All, {}, IdKind::Both, page, kDomainPageSize);
      case LeakSource::Stealer:
      default:
        return api.searchIdentity(item.value, isUsername ? IdKind::Username : IdKind::Email, {}, page, kPageSize);
    }
  };
}

std::vector<LeakSource> WatchService::sourcesFor(WatchKind kind) {
  if (kind == WatchKind::Domain) return {LeakSource::Domain};
  return {LeakSource::Stealer};
}

void WatchService::start() {
  if (thread_.joinable()) return;
  stop_ = false;
  thread_ = std::thread([this] { threadMain(); });
}

void WatchService::stop() {
  stop_ = true;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void WatchService::requestCheck(int64_t watchId) {
  {
    std::lock_guard lock(mutex_);
    if (std::find(queue_.begin(), queue_.end(), watchId) == queue_.end()) queue_.push_back(watchId);
  }
  cv_.notify_all();
}

void WatchService::requestCheckAll() {
  for (const auto& w : db_.listWatches()) requestCheck(w.id);
}

std::string WatchService::progress() const {
  std::lock_guard lock(mutex_);
  return progress_;
}

void WatchService::setProgress(const std::string& s) {
  {
    std::lock_guard lock(mutex_);
    progress_ = s;
  }
  if (changed_) changed_();
}

bool WatchService::dailyDue(int64_t now, int checkMinutes, int64_t lastRun) {
  if (lastRun <= 0) return true;                       // never ran
  if (now - lastRun >= 24 * 3600) return true;         // missed a day (PC off / app closed)
  return localDate(lastRun) != localDate(now) && localMinutesOfDay(now) >= checkMinutes;
}

void WatchService::threadMain() {
  while (!stop_) {
    // Daily schedule
    int checkMinutes = 8 * 60;
    parseHHMM(db_.getSetting(kSettingCheckTime, "08:00"), checkMinutes);
    int64_t lastRun = std::atoll(db_.getSetting(kSettingLastDailyRun, "0").c_str());
    int64_t now = nowEpoch();
    if (dailyDue(now, checkMinutes, lastRun)) {
      db_.setSetting(kSettingLastDailyRun, std::to_string(now));
      requestCheckAll();
    }
    // Values whose first (baseline) check failed are retried every 30 minutes.
    for (const auto& w : db_.listWatches())
      if (!w.baselineDone && now - w.lastCheckAt >= 30 * 60) requestCheck(w.id);

    runQueue();

    std::unique_lock lock(mutex_);
    cv_.wait_for(lock, std::chrono::seconds(30), [this] { return stop_ || !queue_.empty(); });
  }
}

void WatchService::runQueue() {
  for (;;) {
    int64_t id;
    size_t remaining;
    {
      std::lock_guard lock(mutex_);
      if (stop_ || queue_.empty()) break;
      id = queue_.front();
      queue_.pop_front();
      remaining = queue_.size();
    }
    busy_ = true;
    WatchItem w;
    if (db_.getWatch(id, w))
      setProgress("Checking " + w.value + (remaining ? " (" + std::to_string(remaining) + " more queued)" : ""));
    checkItem(id);
    sleepFor(pauseMs(), &stop_);
  }
  if (busy_) {
    busy_ = false;
    setProgress({});
  }
}

void WatchService::checkItem(int64_t watchId) {
  WatchItem item;
  if (!db_.getWatch(watchId, item)) return;

  const int64_t now = nowEpoch();
  const bool baseline = !item.baselineDone;
  const std::vector<LeakSource> sources = sourcesFor(item.kind);
  std::vector<SourceOutcome> outcomes(sources.size());
  // Stored totals: stealer logs / domain leaks share the first column, combolists the second.
  auto previousTotal = [&item](LeakSource s) {
    return s == LeakSource::Combolist ? item.comboTotal : item.stealerTotal;
  };

  for (size_t s = 0; s < sources.size() && !stop_; ++s) {
    SourceOutcome& out = outcomes[s];
    out.attempted = true;
    const int64_t previous = previousTotal(sources[s]);
    if (previous == kTotalUnavailable && !baseline) {  // known not to be on the plan
      out.ok = out.unavailable = true;
      out.total = kTotalUnavailable;
      continue;
    }

    auto first = fetcher_(item, sources[s], 1);
    if (!first.ok) {
      if (first.error.status == 403) {
        out.ok = out.unavailable = true;
        out.total = kTotalUnavailable;
      } else {
        out.error = first.error.message;
      }
      continue;
    }
    if (first.value.blacklisted) {  // protected value: nothing to monitor
      out.ok = true;
      out.total = 0;
      continue;
    }
    out.total = first.value.total;

    // Unchanged total => nothing new (one request per value and dataset per day).
    if (!baseline && out.total == previous) {
      out.ok = true;
      continue;
    }

    const int pageSize = first.value.pageSize > 0 ? first.value.pageSize : kPageSize;
    const int64_t wanted = std::min<int64_t>(out.total, kMaxRecords);
    const int64_t pages = (wanted + pageSize - 1) / pageSize;
    std::vector<LeakRow> all = std::move(first.value.items);
    bool failed = false;
    for (int p = 2; p <= pages && !stop_; ++p) {
      sleepFor(pauseMs(), &stop_);
      auto next = fetcher_(item, sources[s], p);
      if (!next.ok) {
        out.error = next.error.message;
        failed = true;
        break;
      }
      if (next.value.items.empty()) break;
      for (auto& r : next.value.items) all.push_back(std::move(r));
    }
    if (failed || stop_) continue;

    auto seen = db_.seenKeys(item.id, sources[s]);
    std::unordered_set<std::string> batch;
    for (auto& r : all) {
      std::string key = leakKey(r);
      if (seen.count(key) || !batch.insert(key).second) continue;
      out.newRows.push_back(std::move(r));
    }
    out.ok = true;
  }
  if (stop_) return;

  // Persist
  int64_t newTotal = 0;
  std::string sourceNames;
  bool allOk = true;
  std::string status;
  int64_t stealerTotal = item.stealerTotal;
  int64_t comboTotal = sources.size() == 1 ? kTotalUnavailable : item.comboTotal;
  for (size_t s = 0; s < sources.size(); ++s) {
    const SourceOutcome& out = outcomes[s];
    if (!out.ok) {
      allOk = false;
      status = out.error;
      continue;  // a failed dataset keeps its previous total so it is fully re-checked next time
    }
    (sources[s] == LeakSource::Combolist ? comboTotal : stealerTotal) = out.total;
    if (out.newRows.empty()) continue;
    newTotal += static_cast<int64_t>(out.newRows.size());
    if (!sourceNames.empty()) sourceNames += ", ";
    sourceNames += leakSourceLabel(sources[s]);
  }

  int64_t alertId = 0;
  if (!baseline && newTotal > 0) alertId = db_.createAlert(item.id, now, newTotal, sourceNames);
  for (size_t s = 0; s < sources.size(); ++s)
    if (outcomes[s].ok && !outcomes[s].newRows.empty())
      db_.insertSeen(item.id, sources[s], outcomes[s].newRows, now, alertId);

  db_.updateWatchResult(item.id, now, stealerTotal, comboTotal, status, item.baselineDone || allOk);

  if (alertId && notify_) {
    notify_(item.value, std::to_string(newTotal) + (newTotal == 1 ? " new leak found" : " new leaks found") +
                            " (" + sourceNames + ")");
  }
  if (changed_) changed_();
}

}  // namespace su
