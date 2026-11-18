#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "api/ApiClient.h"
#include "core/Database.h"
#include "services/Exporter.h"
#include "services/ReferralClient.h"
#include "services/WatchService.h"
#include "ui/AdvancedForm.h"
#include "ui/Async.h"
#include "ui/ResultsTable.h"
#include "ui/Theme.h"

namespace su::ui {

enum class Page { Search = 0, Monitoring = 1, Referral = 2 };

struct AppContext {
  Database& db;
  ApiClient& api;
  WatchService& watch;
  std::function<void(const std::string& title, const std::string& subtitle, bool error)> toast;
  std::function<void(Page)> navigate;
  std::function<void()> quit;  // close the app for real (after an update)
  Exporter* exporter = nullptr;
  void notify(const std::string& title, const std::string& subtitle = {}, bool error = false) const {
    if (toast) toast(title, subtitle, error);
  }
};

// A paged leak list driven by a "fetch page N" function.
class PagedSearch {
 public:
  using Runner = std::function<Result<LeakPage>(int page, int pageSize)>;

  void run(Runner runner, int page = 1);
  void refresh();  // reload the current page
  bool poll();     // true when a result arrived this frame
  void clear();
  void setSampleData(LeakPage page);

  bool running() const { return op_.running(); }
  bool hasResult() const { return has_; }
  const LeakPage& result() const { return result_; }
  LeakPage& mutableResult() { return result_; }  // rows filled in after an unlock
  const std::string& error() const { return error_; }
  const Runner& runner() const { return runner_; }
  int& page() { return page_; }

  int pageSize = 100;
  const char* emptyHint = nullptr;  // shown before the first search

 private:
  Async<Result<LeakPage>> op_;
  Runner runner_;
  LeakPage result_;
  std::string error_;
  bool has_ = false;
  int page_ = 1;
};

class SearchView {
 public:
  explicit SearchView(AppContext& ctx);
  void draw();
  void loadSampleData();  // sample data
  void setAdvanced(bool on) { advanced_ = on; }

 private:
  enum Tab { TabLookup, TabDarkWeb, TabCount };

  struct Actions {
    std::string exportName;                                 // empty = no export
    int exportPageSize = 100;
    std::string monitorValue;                               // empty = no monitoring
    WatchKind monitorKind = WatchKind::Email;
    TableOptions table;
  };

  bool searchRow(const char* id, const char* hint, std::string& value, bool enabled = true);
  void drawResults(PagedSearch& s, const Actions& actions);
  void addToWatch(const std::string& value, WatchKind kind);
  void queueUnlock(const std::string& id);
  void cancelUnlocks();
  void pollUnlocks();

  void drawLookupTab();      // email / username / domain in one box
  void drawIdentityResults();
  void drawDomainResults();
  void runDomainList();
  void drawAdvanced();  // advanced filters inside the Email / Domain tab
  void drawDarkWebTab();

  AppContext& ctx_;
  int tab_ = TabLookup;

  // Password unlocking in email and domain search: first kUnlockLimit rows, one at a time, kLineDelay apart.
  static constexpr int kUnlockLimit = 30;
  static constexpr double kLineDelay = 0.25;        // pause between one line and the next
  static constexpr double kMinUnlockSeconds = 0.8;  // "Unlocking..." stays at least this long
  std::deque<std::string> unlockQueue_;
  std::unordered_set<std::string> unlocking_;       // queued + in flight
  std::string inFlightId_;
  Async<Result<std::vector<LeakRow>>> unlockOp_;
  double nextUnlockAt_ = 0.0;

  // Email / domain lookup: one input, the kind of value decides which search runs
  enum class Lookup { None, Identity, Domain };
  std::string lookupValue_;
  int lookupMode_ = 0;  // 0 detect automatically, 1 email or username, 2 domain
  Lookup lookupShown_ = Lookup::None;

  // Email / username results
  std::string idFilter_;
  int idKind_ = 0;
  std::string idShown_, idShownFilter_;  // the query behind the displayed results
  int idShownKind_ = 0;
  PagedSearch idSearch_;

  // Domain results
  std::string domainFilter_, domainShown_;
  int domainCategory_ = -1;  // -1 = all categories
  Async<Result<DomainSummary>> domainSummaryOp_;
  std::optional<DomainSummary> domainSummary_;
  std::string domainError_;
  PagedSearch domainSearch_;

  // Advanced search ("Advanced search" checkbox on the Email / Domain tab)
  bool advanced_ = false;
  bool advEditing_ = true;  // form open; collapsed to a summary once results are shown
  AdvancedForm advForm_;
  AdvQuery advShown_;
  std::string advShownSummary_;
  PagedSearch advSearch_;

  // Dark web
  std::string dwQuery_, dwShown_;
  Async<Result<DarkWebPage>> dwOp_;
  DarkWebPage dwPage_;
  bool dwHas_ = false;
  int dwPageNo_ = 1;
  std::string dwError_;
  std::unordered_set<std::string> dwExpanded_;
};

class WatchlistView {
 public:
  explicit WatchlistView(AppContext& ctx);
  void draw();
  void markDirty() { dirty_ = true; }

 private:
  void reload();

  AppContext& ctx_;
  std::atomic<bool> dirty_{true};
  std::vector<WatchItem> items_;
  std::string newValue_;
  int newKind_ = 0;  // 0 auto (email/username), 1 email, 2 username, 3 domain
  std::string addError_;
  std::string checkTime_;
  std::string checkTimeError_;
  bool autostart_ = false;
  bool minimizeToTray_ = true;
  int64_t selected_ = 0;
  std::vector<LeakRow> selectedLeaks_;
  int64_t confirmRemove_ = 0;
};

// Referral programme: your ID / invite link, who invited you, referred users and rewards.
class ReferralView {
 public:
  ReferralView(AppContext& ctx, ReferralClient& client);
  void draw();
  void poll();     // every frame (also when the tab is hidden)
  void refresh();  // fetch referred users from the referral server
  size_t count() const;
  void loadSampleData();  // sample data

 private:
  AppContext& ctx_;
  ReferralClient& client_;
  Async<Result<ReferralStatus>> fetchOp_;
  std::optional<ReferralStatus> status_;
  std::string error_;
  double lastFetch_ = -1000.0;

  // Invited by: one ID, saved once (then the box and button stay disabled).
  void drawInvitedBy();
  std::string invitedBy_;      // saved inviter ID (empty = not set yet)
  std::string invitedInput_;
  std::string pendingInviter_; // being confirmed / sent
  bool openInviteConfirm_ = false;
  Async<Result<bool>> inviteOp_;
  // Inviter that came stamped into the download (set automatically, cannot be changed).
  bool invitedFromDownload_ = false;
  bool stampRegisterPending_ = false;  // still to be registered with the referral server
  bool silentInvite_ = false;          // the in-flight registration is the stamped one (no toasts)

  // Register this user's own referral ID with the server, once (retried on later launches if it fails).
  Async<Result<bool>> selfRegOp_;
  bool selfRegistered_ = false;
  bool selfRegPending_ = false;
};

}  // namespace su::ui
