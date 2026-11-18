#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <thread>

#include "core/Util.h"
#include "ui/Theme.h"
#include "ui/Views.h"
#include "ui/Widgets.h"

namespace su::ui {

using namespace color;

// ---------------------------------------------------------------------------
// PagedSearch
// ---------------------------------------------------------------------------
void PagedSearch::run(Runner runner, int page) {
  runner_ = std::move(runner);
  page_ = page;
  error_.clear();
  auto r = runner_;
  const int ps = pageSize;
  op_.start([r, page, ps] { return r(page, ps); });
}

void PagedSearch::refresh() {
  if (runner_) run(runner_, page_);
}

bool PagedSearch::poll() {
  auto done = op_.take();
  if (!done) return false;
  if (done->ok) {
    result_ = std::move(done->value);
    has_ = true;
  } else {
    error_ = done->error.message;
  }
  return true;
}

void PagedSearch::clear() {
  op_ = {};
  has_ = false;
  result_ = {};
  error_.clear();
  page_ = 1;
}

void PagedSearch::setSampleData(LeakPage page) {
  result_ = std::move(page);
  has_ = true;
  error_.clear();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {
const char* kIdKinds[] = {"Emails & usernames", "Emails only", "Usernames only"};

float barHeight() { return ImGui::GetFontSize() * 2.75f; }

void smallInput(const char* id, const char* hint, std::string& value, float width, bool* enter = nullptr) {
  ImGui::SetNextItemWidth(width);
  bool e = ImGui::InputTextWithHint(id, hint, &value, ImGuiInputTextFlags_EnterReturnsTrue);
  if (enter) *enter = e;
}

void smallCombo(const char* id, int& value, const char* const* items, int count, float width) {
  ImGui::SetNextItemWidth(width);
  ImGui::Combo(id, &value, items, count);
}

std::string compactCount(int64_t v) { return formatThousands(v); }
}  // namespace

SearchView::SearchView(AppContext& ctx) : ctx_(ctx) {
}

bool SearchView::searchRow(const char* id, const char* hint, std::string& value, bool enabled) {
  const float h = barHeight();
  const char* label = ICON_SEARCH "  Search";
  const float bw = w::buttonWidth(label) + ImGui::GetFontSize();
  const float avail = ImGui::GetContentRegionAvail().x;
  bool enter = w::searchBar(id, hint, value, avail - bw - ImGui::GetStyle().ItemSpacing.x, h);
  ImGui::SameLine();
  ImGui::PushID(id);
  bool click = w::button(label, w::ButtonKind::Primary, ImVec2(bw, h), enabled && !trim(value).empty());
  ImGui::PopID();
  return (enter || click) && enabled && !trim(value).empty();
}

void SearchView::addToWatch(const std::string& rawValue, WatchKind kind) {
  std::string value = trim(rawValue);
  if (value.empty()) return;
  if (int64_t id = ctx_.db.addWatch(value, kind)) {
    ctx_.watch.requestCheck(id);
    ctx_.notify(ICON_BELL "  Monitoring added", value + " will be checked every day.");
  } else {
    ctx_.notify(ICON_BELL "  Already monitored", value + " is already on the monitoring list.");
  }
}

// ---------------------------------------------------------------------------
// Results block (header with actions + table)
// ---------------------------------------------------------------------------
void SearchView::drawResults(PagedSearch& s, const Actions& a) {
  if (s.poll() && a.table.unlockLimit > 0) cancelUnlocks();  // queued rows belonged to the previous page

  if (s.running()) spinner("Searching...");
  if (!s.error().empty()) errorText(s.error());
  if (!s.hasResult()) {
    if (!s.running() && s.error().empty() && s.emptyHint) {
      ImGui::Spacing();
      mutedText(s.emptyHint);
    }
    return;
  }
  const LeakPage& res = s.result();
  if (res.blacklisted) {
    mutedText("This value is protected and can't be searched.");
    return;
  }

  // ---- header: "147 results"  [pager]            [Show all] [Export] [Add Monitoring]
  const float rowY = ImGui::GetCursorPosY();
  ImGui::AlignTextToFramePadding();
  ImGui::PushFont(fonts().semibold);
  ImGui::TextUnformatted(formatThousands(res.total).c_str());
  ImGui::PopFont();
  ImGui::SameLine(0, 4);
  w::textMuted(res.total == 1 ? "result" : "results");
  if (res.total > res.pageSize) {
    ImGui::SameLine(0, 18);
    if (drawPager("pager", s.page(), res.total, res.pageSize, s.running())) s.run(s.runner(), s.page());
  }

  // Show all / Hide all: "Hide all" while any row on this page is shown or being unlocked.
  auto& revealed = revealedRows();
  const int limit = a.table.unlockLimit;
  auto canUnlock = [limit](const LeakRow& r, size_t i) {
    return r.password.empty() && !r.id.empty() && static_cast<int>(i) < limit;
  };
  bool anyShowable = false, anyOpen = false;
  for (size_t i = 0; i < res.items.size(); ++i) {
    const LeakRow& r = res.items[i];
    anyShowable |= !r.password.empty() || canUnlock(r, i);
    anyOpen |= revealed.count(leakKey(r)) > 0 || (!r.id.empty() && unlocking_.count(r.id) > 0);
  }
  const bool anyPassword = anyShowable || anyOpen;

  const bool exporting = ctx_.exporter && ctx_.exporter->status().running;
  const char* eyeLabel = anyOpen ? ICON_HIDE "  Hide all" : ICON_EYE "  Show all";
  const char* exportLabel = ICON_DOWNLOAD "  Export";
  const char* monitorLabel = ICON_BELL "  Add Monitoring";
  float total = 0, sp = ImGui::GetStyle().ItemSpacing.x;
  if (anyPassword) total += w::buttonWidth(eyeLabel) + sp;
  if (!a.exportName.empty()) total += w::buttonWidth(exportLabel) + sp;
  if (!a.monitorValue.empty()) total += w::buttonWidth(monitorLabel) + sp;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX(), w::contentMaxX() - total + sp));
  ImGui::SetCursorPosY(rowY - 2.0f);

  bool first = true;
  auto next = [&first] {
    if (!first) ImGui::SameLine();
    first = false;
  };
  if (anyPassword) {
    next();
    if (w::button(eyeLabel, w::ButtonKind::Outline)) {
      if (anyOpen) {
        for (const auto& r : res.items) revealed.erase(leakKey(r));
        if (limit > 0) cancelUnlocks();
      } else {
        // Rows that already have their password open one line after another; locked rows
        // (first `limit` only) are unlocked one by one in order.
        const double now = ImGui::GetTime();
        const double step = a.table.rowDelay > 0 ? kLineDelay : 0.0;
        int k = 0;
        for (size_t i = 0; i < res.items.size(); ++i) {
          const LeakRow& r = res.items[i];
          if (!r.password.empty()) revealed[leakKey(r)] = now + step * ++k;
          else if (canUnlock(r, i)) queueUnlock(r.id);
        }
      }
    }
    if (ImGui::IsItemHovered()) {
      if (anyOpen) ImGui::SetTooltip("Hide every password on this page");
      else if (limit > 0) ImGui::SetTooltip("Show passwords line by line (first %d rows)", limit);
      else ImGui::SetTooltip("Show every password (or use the eye on a row)");
    }
  }
  if (!a.exportName.empty()) {
    next();
    if (w::button(exportLabel, w::ButtonKind::Outline, ImVec2(0, 0), !exporting && res.total > 0 && s.runner())) {
      if (!ctx_.exporter->start(s.runner(), a.exportPageSize, res.total, a.exportName))
        ctx_.notify(ICON_WARNING "  Export already running", "Wait for the current export to finish.", true);
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Save all results (up to %s rows) as CSV in your Downloads folder.",
                        formatThousands(Exporter::kMaxRows).c_str());
  }
  if (!a.monitorValue.empty()) {
    next();
    if (w::button(monitorLabel, w::ButtonKind::Outline)) addToWatch(a.monitorValue, a.monitorKind);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Check \"%s\" every day and alert on new leaks.", a.monitorValue.c_str());
  }
  if (first) ImGui::NewLine();  // no buttons: finish the "N results" line

  if (res.items.empty()) {
    mutedText("No leaks found.");
    return;
  }
  TableOptions opt = a.table;
  if (limit > 0) opt.unlocking = &unlocking_;
  const int clicked = drawResultsTable("results", res.items, opt);
  if (clicked >= 0 && limit > 0) queueUnlock(res.items[static_cast<size_t>(clicked)].id);
}

// ---------------------------------------------------------------------------
// Password unlocking (email search): one row at a time, a short pause between lines
// ---------------------------------------------------------------------------
void SearchView::queueUnlock(const std::string& id) {
  if (id.empty() || unlocking_.count(id)) return;
  unlocking_.insert(id);
  unlockQueue_.push_back(id);
}

void SearchView::cancelUnlocks() {
  unlockQueue_.clear();
  unlocking_.clear();
  if (!inFlightId_.empty()) unlocking_.insert(inFlightId_);  // finishes, but nothing else follows
}

void SearchView::pollUnlocks() {
  const double now = ImGui::GetTime();
  if (auto r = unlockOp_.take()) {
    const std::string doneId = inFlightId_;
    inFlightId_.clear();
    unlocking_.erase(doneId);
    if (r->ok) {
      bool got = false;
      for (const LeakRow& u : r->value) {
        const std::string& uid = u.id.empty() ? doneId : u.id;
        // The row lives in the email or the domain results (both are stealer-log records).
        for (PagedSearch* search : {&idSearch_, &domainSearch_, &advSearch_})
        for (LeakRow& row : search->mutableResult().items) {
          if (row.id != uid || u.password.empty()) continue;
          row.password = u.password;
          row.unlocked = true;
          if (row.url.empty()) row.url = u.url;
          if (!u.username.empty()) row.username = u.username;
          if (u.strength) row.strength = u.strength;
          revealedRows()[leakKey(row)] = now;  // appears right away
          got = true;
        }
      }
      if (!got) {
        ctx_.notify(ICON_WARNING "  Password not available", "This record could not be unlocked.", true);
        cancelUnlocks();
      }
    } else {
      ctx_.notify(ICON_WARNING "  Unlock failed", r->error.message, true);
      cancelUnlocks();
    }
    nextUnlockAt_ = now + kLineDelay;
  }
  if (!unlockOp_.running() && !unlockQueue_.empty() && now >= nextUnlockAt_) {
    inFlightId_ = unlockQueue_.front();
    unlockQueue_.pop_front();
    auto& api = ctx_.api;
    std::vector<std::string> ids{inFlightId_};
    const auto minSpin = std::chrono::milliseconds(static_cast<int>(kMinUnlockSeconds * 1000));
    unlockOp_.start([&api, ids, minSpin] {
      const auto t0 = std::chrono::steady_clock::now();
      auto result = api.unlockRows(ids);
      const auto spent = std::chrono::steady_clock::now() - t0;
      if (spent < minSpin) std::this_thread::sleep_for(minSpin - spent);  // keep "Unlocking..." visible
      return result;
    });
  }
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------
void SearchView::draw() {
  static const char* tabs[] = {"Email / Domain", "Dark Web"};
  const float tabY = ImGui::GetCursorPosY();
  w::tabStrip("search_tabs", tabs, TabCount, tab_);
  const float afterTabs = ImGui::GetCursorPosY();
  if (tab_ == TabLookup) {
    // "Advanced search" checkbox at the right end of the tab row.
    const char* label = "Advanced search";
    const float box = ImGui::GetFontSize() + 6.0f;  // checkbox square with 3 px padding
    const float cw = box + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
    ImGui::SameLine(w::contentMaxX() - cw);
    ImGui::SetCursorPosY(tabY + (afterTabs - tabY - ImGui::GetStyle().ItemSpacing.y - box) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, IM_COL32_WHITE);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, advanced_ ? Accent : PanelActive);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, advanced_ ? AccentHover : PanelHover);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));
    ImGui::Checkbox(label, &advanced_);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Combine include / exclude filters on URLs, logins, passwords and email parts.");
    ImGui::SetCursorPosY(afterTabs);
  }
  pollUnlocks();
  switch (tab_) {
    case TabLookup: drawLookupTab(); break;
    case TabDarkWeb: drawDarkWebTab(); break;
    default: break;
  }
}

void SearchView::drawLookupTab() {
  if (advanced_) {
    drawAdvanced();
    return;
  }
  const float fs = ImGui::GetFontSize();
  auto& api = ctx_.api;

  // ---- one search box for email addresses, usernames and domains
  if (searchRow("lookup_bar", "Email address, username or domain (e.g. john@example.com or example.com)",
                lookupValue_)) {
    const std::string value = trim(lookupValue_);
    const std::string asDomain = normalizeDomain(value);
    const bool domain = lookupMode_ == 2 || (lookupMode_ == 0 && !looksLikeEmail(value) && looksLikeDomain(asDomain));
    if (domain) {
      lookupShown_ = Lookup::Domain;
      domainShown_ = asDomain;
      domainCategory_ = -1;
      domainSummary_.reset();
      domainError_.clear();
      domainSearch_.clear();
      std::string d = domainShown_;
      domainSummaryOp_.start([&api, d] { return api.domainSummary(d); });
      runDomainList();
    } else {
      lookupShown_ = Lookup::Identity;
      idShown_ = value;
      idShownFilter_ = idFilter_;
      idShownKind_ = idKind_;
      std::string v = idShown_, filter = idShownFilter_;
      IdKind kind = static_cast<IdKind>(idShownKind_);
      idSearch_.run([&api, v, filter, kind](int p, int ps) { return api.searchIdentity(v, kind, filter, p, ps); });
    }
  }
  if (auto r = domainSummaryOp_.take()) {
    if (r->ok) domainSummary_ = r->value;
    else domainError_ = r->error.message;
  }

  // ---- options row: [Detect automatically v] (+ filters for the shown kind)      Domain results for x
  static const char* modes[] = {"Detect automatically", "Email or username", "Domain"};
  smallCombo("##lookup_mode", lookupMode_, modes, IM_ARRAYSIZE(modes), fs * 11);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("An address with @ searches emails, a name like example.com searches the domain,\n"
                      "anything else searches usernames. Pick a type here to override.");
  bool filterEnter = false;
  if (lookupShown_ == Lookup::Identity) {
    ImGui::SameLine();
    smallCombo("##id_kind", idKind_, kIdKinds, IM_ARRAYSIZE(kIdKinds), fs * 11);
    ImGui::SameLine();
    smallInput("##id_filter", ICON_FILTER "  Filter by URL or username (optional)", idFilter_, fs * 20, &filterEnter);
    if (filterEnter) {
      idShownFilter_ = idFilter_;
      idShownKind_ = idKind_;
      std::string v = idShown_, filter = idShownFilter_;
      IdKind kind = static_cast<IdKind>(idShownKind_);
      idSearch_.run([&api, v, filter, kind](int p, int ps) { return api.searchIdentity(v, kind, filter, p, ps); });
    }
  } else if (lookupShown_ == Lookup::Domain) {
    ImGui::SameLine();
    smallInput("##domain_filter", ICON_FILTER "  Filter results (Enter)", domainFilter_, fs * 20, &filterEnter);
    if (filterEnter) runDomainList();
  }
  if (lookupShown_ != Lookup::None) {
    const bool isDomain = lookupShown_ == Lookup::Domain;
    const std::string& shown = isDomain ? domainShown_ : idShown_;
    const char* kind = isDomain ? "Domain" : looksLikeEmail(shown) ? "Email" : "Username";
    std::string text = std::string(isDomain ? ICON_GLOBE : ICON_PERSON) + "  " + kind + " results for " + shown;
    const float tw = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + fs, w::contentMaxX() - tw));
    ImGui::AlignTextToFramePadding();
    w::textColored(isDomain ? Blue : Green, text.c_str());
  }

  switch (lookupShown_) {
    case Lookup::Identity: drawIdentityResults(); break;
    case Lookup::Domain: drawDomainResults(); break;
    case Lookup::None:
      ImGui::Spacing();
      mutedText("Enter an email address or username to search stealer logs, or a domain to see leaked credentials "
                "of its employees, customers and third-party accounts.");
      break;
  }
}

void SearchView::drawIdentityResults() {
  Actions a;
  a.exportName = "leaks_" + idShown_;
  a.monitorValue = idShown_;
  a.monitorKind = idShownKind_ == 2 ? WatchKind::Username
                  : idShownKind_ == 1 ? WatchKind::Email
                                      : (looksLikeEmail(idShown_) ? WatchKind::Email : WatchKind::Username);
  a.table.showUrl = true;
  // Email and domain search: the eye unlocks a locked row (first 30 rows); "Show all" unlocks them line by line.
  a.table.rowDelay = 0.8;            // already-unlocked rows: short "Unlocking..." before showing
  a.table.unlockLimit = kUnlockLimit; // locked rows: only the first 30 can be unlocked
  drawResults(idSearch_, a);
}

void SearchView::runDomainList() {
  auto& api = ctx_.api;
  std::string d = domainShown_, filter = domainFilter_;
  auto cat = domainCategory_ < 0 ? DomainCategory::All : static_cast<DomainCategory>(domainCategory_);
  domainSearch_.run([&api, d, cat, filter](int p, int ps) { return api.domainLeaks(d, cat, filter, IdKind::Both, p, ps); });
}

void SearchView::drawDomainResults() {
  if (!domainError_.empty()) errorText(domainError_);
  if (domainSummary_ && domainSummary_->blacklisted) {
    mutedText("This domain is protected and can't be searched.");
    return;
  }

  // ---- category cards (click to filter, click again for all)
  struct Card { int cat; const char* icon; const char* label; ImU32 color; int64_t count; };
  const DomainSummary ds = domainSummary_.value_or(DomainSummary{});
  const Card cards[] = {
      {static_cast<int>(DomainCategory::Employees), ICON_PERSON, "Employees", Green, ds.employees},
      {static_cast<int>(DomainCategory::ThirdParties), ICON_BRIEFCASE, "Third-party accounts", Blue, ds.thirdParties},
      {static_cast<int>(DomainCategory::Customers), ICON_PEOPLE, "Customers", Orange, ds.customers},
  };
  const float sp = ImGui::GetStyle().ItemSpacing.x;
  const float cw = (ImGui::GetContentRegionAvail().x - sp * 2) / 3.0f;
  for (int i = 0; i < 3; ++i) {
    if (i) ImGui::SameLine();
    std::string count = domainSummary_ ? compactCount(cards[i].count) + (ds.approximate ? "~" : "")
                        : domainSummaryOp_.running() ? "..." : "-";
    ImGui::PushID(i);
    if (w::categoryCard("card", cards[i].icon, cards[i].label, count, cards[i].color,
                        domainCategory_ == cards[i].cat, cw)) {
      domainCategory_ = domainCategory_ == cards[i].cat ? -1 : cards[i].cat;
      runDomainList();
    }
    ImGui::PopID();
  }

  Actions a;
  auto cat = domainCategory_ < 0 ? DomainCategory::All : static_cast<DomainCategory>(domainCategory_);
  static const char* catNames[] = {"_employees", "_customers", "_third_parties", ""};
  a.exportName = "leaks_" + domainShown_ + catNames[static_cast<int>(cat)];
  a.exportPageSize = 1000;
  a.monitorValue = domainShown_;
  a.monitorKind = WatchKind::Domain;
  a.table.infoLabel = cat == DomainCategory::All ? "CATEGORY" : nullptr;
  a.table.rowDelay = 0.8;             // same show / hide / unlock behaviour as email search
  a.table.unlockLimit = kUnlockLimit; // locked rows: only the first 30 can be unlocked
  drawResults(domainSearch_, a);
}

void SearchView::drawAdvanced() {
  auto& api = ctx_.api;
  if (advEditing_ || !advSearch_.hasResult()) {
    // The form is taller than the window: it scrolls; results appear once Search is pressed.
    ImGui::BeginChild("adv_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    if (advForm_.draw()) {
      advShown_ = advForm_.query();
      advShownSummary_ = advForm_.summary();
      AdvQuery q = advShown_;
      advSearch_.run([&api, q](int p, int ps) { return api.searchAdvanced(q, p, ps); });
      advEditing_ = false;
    }
    if (advSearch_.running()) spinner("Searching...");
    if (!advSearch_.error().empty()) errorText(advSearch_.error());
    ImGui::EndChild();
    if (advSearch_.poll()) cancelUnlocks();
    return;
  }

  // ---- collapsed filters: [filter icon] Advanced search  3 values in 2 fields: ...   [Edit filters]
  const float fs = ImGui::GetFontSize();
  ImVec2 p = ImGui::GetCursorScreenPos();
  const float wdt = ImGui::GetContentRegionAvail().x;
  const float h = fs * 3.0f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  w::roundedGradient(dl, p, ImVec2(p.x + wdt, p.y + h), 10.0f, withAlpha(Accent, 40), withAlpha(Accent, 8), p.x, p.x + wdt);
  dl->AddRect(p, ImVec2(p.x + wdt, p.y + h), withAlpha(Accent, 90), 10.0f);
  ImGui::SetCursorScreenPos(ImVec2(p.x + fs, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
  w::textColored(Violet, ICON_FILTER);
  ImGui::SameLine(0, 8);
  ImGui::PushFont(fonts().semibold);
  ImGui::TextUnformatted("Advanced search");
  ImGui::PopFont();
  ImGui::SameLine(0, 12);
  w::textMuted(advShownSummary_.c_str());
  const char* edit = ICON_FILTER "  Edit filters";
  const float ew = w::buttonWidth(edit);
  ImGui::SetCursorScreenPos(ImVec2(p.x + wdt - fs * 0.6f - ew, p.y + (h - w::buttonHeight()) * 0.5f));
  if (w::button(edit, w::ButtonKind::Outline)) advEditing_ = true;
  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 6.0f));
  ImGui::Dummy(ImVec2(0, 0));

  Actions a;
  a.exportName = "leaks_advanced_search";
  a.table.showUrl = true;
  a.table.rowDelay = 0.8;
  a.table.unlockLimit = kUnlockLimit;
  drawResults(advSearch_, a);
}

void SearchView::drawDarkWebTab() {
  auto& api = ctx_.api;
  auto run = [this, &api](int page) {
    dwPageNo_ = page;
    std::string q = dwShown_;
    dwOp_.start([&api, q, page] { return api.searchDarkWeb(q, page); });
  };
  if (searchRow("dw_bar", "Company, domain, IP address or email mentioned in forum posts", dwQuery_)) {
    dwShown_ = trim(dwQuery_);
    dwHas_ = false;
    dwError_.clear();
    dwExpanded_.clear();
    run(1);
  }
  if (auto r = dwOp_.take()) {
    if (r->ok) {
      dwPage_ = std::move(r->value);
      dwHas_ = true;
      dwExpanded_.clear();
    } else {
      dwError_ = r->error.message;
    }
  }
  if (dwOp_.running()) spinner("Searching...");
  if (!dwError_.empty()) errorText(dwError_);
  if (!dwHas_) {
    if (!dwOp_.running()) {
      ImGui::Spacing();
      mutedText("Find mentions of your company, domains or people in posts from dark web forums and markets.");
    }
    return;
  }

  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::PushFont(fonts().semibold);
  ImGui::TextUnformatted(formatThousands(dwPage_.total).c_str());
  ImGui::PopFont();
  ImGui::SameLine(0, 4);
  w::textMuted(dwPage_.total == 1 ? "post" : "posts");
  if (dwPage_.total > dwPage_.pageSize) {
    ImGui::SameLine(0, 18);
    int page = dwPageNo_;
    if (drawPager("dw_pager", page, dwPage_.total, dwPage_.pageSize, dwOp_.running())) run(page);
  }
  if (dwPage_.items.empty()) {
    mutedText("No posts found.");
    return;
  }

  ImGui::BeginChild("dw_list", ImVec2(0, 0));
  for (size_t i = 0; i < dwPage_.items.size(); ++i) {
    const DarkWebPost& p = dwPage_.items[i];
    const std::string key = p.id.empty() ? std::to_string(i) : p.id;
    ImGui::PushID(static_cast<int>(i));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Panel);
    ImGui::BeginChild("post", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PushFont(fonts().semibold);
    ImGui::TextWrapped("%s", p.title.empty() ? "(untitled post)" : p.title.c_str());
    ImGui::PopFont();
    if (!p.source.empty()) {
      w::badge(p.source.c_str(), Violet);
      ImGui::SameLine(0, 10);
    }
    if (!p.target.empty()) {
      w::badge(p.target.c_str(), Orange);
      ImGui::SameLine(0, 10);
    }
    ImGui::AlignTextToFramePadding();
    std::string meta;
    if (!p.author.empty()) meta += std::string(ICON_PERSON " ") + p.author + "    ";
    const std::string& date = p.publishedAt.empty() ? p.ingestedAt : p.publishedAt;
    if (!date.empty()) meta += std::string(ICON_CALENDAR " ") + shortDate(date);
    w::textMuted(meta.c_str());

    const bool expanded = dwExpanded_.count(key) > 0;
    const size_t limit = 420;
    std::string body = p.content;
    const bool longPost = body.size() > limit;
    if (longPost && !expanded) body = body.substr(0, limit) + "...";
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(body.empty() ? "(no content)" : body.c_str());
    ImGui::PopTextWrapPos();
    if (longPost) {
      if (w::button(expanded ? "Show less" : "Show more", w::ButtonKind::Ghost)) {
        if (expanded) dwExpanded_.erase(key);
        else dwExpanded_.insert(key);
      }
    }
    if (p.censored) w::textMuted(ICON_LOCK "  Some sensitive content is censored on the current plan.");
    if (!p.sourceRef.empty()) {
      ImGui::PushFont(fonts().mono);
      w::textMuted(p.sourceRef.c_str());
      ImGui::PopFont();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right-click to copy the source reference");
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        ImGui::SetClipboardText(p.sourceRef.c_str());
        ctx_.notify(ICON_COPY "  Copied", "Source reference copied to the clipboard.");
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopID();
  }
  ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Sample data (fictional company and records)
// ---------------------------------------------------------------------------
void SearchView::loadSampleData() {
  tab_ = TabLookup;
  advForm_.loadSampleData();
  lookupValue_ = domainShown_ = "acme-corp.com";
  lookupShown_ = Lookup::Domain;
  DomainSummary ds;
  ds.employees = 89;
  ds.thirdParties = 41;
  ds.customers = 17;
  domainSummary_ = ds;
  struct D { const char* url; const char* user; const char* pass; const char* date; const char* cat; bool email; bool unlocked; };
  const D rows[] = {
      {"acme-corp.com/login", "j.martin@acme-corp.com", "Acm3Corp!2023", "2026-06-07", "employees", true, true},
      {"login.microsoftonline.com", "s.bernard@acme-corp.com", "Summer@2024", "2026-06-03", "employees", true, true},
      {"github.com/login", "admin@acme-corp.com", "Adm1n$ecure9", "2026-05-21", "third_parties", true, true},
      {"www.linkedin.com/login", "p.durand@acme-corp.com", "Qwerty!789xZ", "2025-08-15", "employees", true, true},
      {"dropbox.com/login", "c.leroy@acme-corp.com", "", "2026-02-10", "third_parties", true, false},
      {"vpn.acme-corp.com", "jdurand", "", "2024-12-10", "employees", false, false},
      {"shop.acme-corp.com/account", "m.petit@gmail.com", "", "2024-10-02", "customers", true, false},
  };
  LeakPage p;
  int n = 0;
  for (const D& d : rows) {
    LeakRow r;
    r.id = "s" + std::to_string(n++);
    r.url = d.url;
    r.username = d.user;
    r.password = d.pass;
    r.unlocked = d.unlocked;
    r.isEmail = d.email;
    r.addedAt = std::string(d.date) + "T00:00:00Z";
    r.info = std::string(d.cat) == "employees" ? "Employee" : std::string(d.cat) == "customers" ? "Customer" : "Third party";
    r.strength = 6;
    p.items.push_back(r);
  }
  p.total = 147;
  p.totalUnlocked = 4;
  p.pageSize = 100;
  // Two rows opened with their eye button, the rest hidden.
  revealedRows()[leakKey(p.items[0])] = 0.0;
  revealedRows()[leakKey(p.items[2])] = 0.0;
  revealedRows()[leakKey(p.items[1])] = 1e9;  // still "Unlocking..." in the screenshot
  domainSearch_.setSampleData(p);
}

}  // namespace su::ui
