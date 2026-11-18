#pragma once
#include <atomic>
#include <chrono>
#include <optional>
#include <string>

#include "services/AgentRunner.h"
#include "services/Updater.h"
#include "ui/IndexGraph.h"
#include "ui/Views.h"

namespace su::ui {

// Top-level UI: header bar, live stats banner, pages, toasts.
class App {
 public:
  explicit App(AppContext& ctx);

  void render();
  // Background work (update checks, companion updater, referral check-ins). Runs every main-loop iteration,
  // including while the app is hidden in the tray and nothing is drawn. Wall-clock timed.
  void tick();
  void showMonitoring();                     // e.g. after clicking a desktop notification
  void selectPage(Page p) { page_ = p; }
  void markDirty();                          // thread-safe: watch data changed
  void loadSampleData();                           // sample data
  void showAdvancedSearch(bool on) { search_.setAdvanced(on); }  // screenshot tool

 private:
  void poll();
  double nowSeconds() const;  // seconds since start, from a wall clock that keeps advancing while hidden
  void drawHeader();
  LiveFigures liveFigures();  // live counter values for the graph card (advances every frame)
  void drawToast();
  void drawReferralCard();
  void drawReferralNote();
  void startUpdate();
  void installPendingUpdate();
  float referralCardHeight() const;
  void toast(const std::string& title, const std::string& subtitle, bool error);

  AppContext& ctx_;
  Exporter exporter_;
  SearchView search_;
  WatchlistView watchlist_;
  ReferralClient referralClient_;
  ReferralView referral_;
  bool referralNoteHidden_ = false;  // closed for this session
  Page page_ = Page::Search;

  IndexGraph graph_;
  // Updates are automatic: checked shortly after start, then every hour. A newer version is downloaded in
  // the background and installed (the app restarts) as soon as no monitoring check or export is running.
  // A failed download or install is retried at the next check.
  Updater updater_;
  Async<Result<std::optional<UpdateInfo>>> updateCheckOp_;
  Async<Result<std::filesystem::path>> updateDownloadOp_;
  std::optional<UpdateInfo> update_;
  std::filesystem::path pendingInstall_;  // downloaded update waiting for the app to be idle
  const std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();
  double lastUpdateCheck_ = -1e9;  // far in the past: the first check runs right after start
  std::string updateStatus_;       // result of the last check / update, shown on the version badge

  // Companion "updater.exe": downloaded and launched ~5 s after start, then re-checked hourly. When it is
  // running, the update check tells the server, which shows the updater as active for this user.
  AgentRunner agent_;
  Async<bool> agentOp_;
  bool agentRunning_ = false;
  double lastAgentCheck_ = -1e9;
  Async<Result<LiveStats>> statsOp_;
  Async<Result<IndexStats>> indexOp_;
  std::optional<LiveStats> stats_;
  std::optional<IndexStats> index_;
  double lastStatsFetch_ = -1000.0;
  double statsAt_ = 0.0;         // when stats_ arrived; counters advance from there at the live rate
  double shownLines_ = 0.0;      // indexed-lines counter as displayed (never goes backwards)
  double statsFlashAt_ = -100.0; // fresh figures arrived: ripple burst + green glow
  double lastIndexFetch_ = -1000.0;

  struct Toast {
    std::string title, subtitle;
    bool error = false;
    double until = 0;
  } toast_;
};

}  // namespace su::ui
