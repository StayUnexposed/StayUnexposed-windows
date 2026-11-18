#include "ui/App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <ctime>

#include "AppInfo.h"
#include "core/HardwareId.h"
#include "core/Util.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace su::ui {

using namespace color;

namespace {
// The Stay Unexposed mark: an eye with a magnifier handle (same shape as the app icon and the website),
// drawn in the SVG's coordinates scaled to `size` and centred on `c`. `fg` is the mark, `bg` the colour
// behind it (used for the iris highlight).
void drawBrandMark(ImDrawList* dl, ImVec2 c, float size, ImU32 fg, ImU32 bg) {
  const float k = size / 21.0f;
  const auto P = [&](float sx, float sy) { return ImVec2(c.x + (sx - 11.5f) * k, c.y + (sy - 11.1f) * k); };
  float stroke = k * 1.7f;
  if (stroke < 1.3f) stroke = 1.3f;
  // eye outline (almond)
  dl->PathClear();
  dl->PathLineTo(P(1.0f, 11.0f));
  dl->PathBezierQuadraticCurveTo(P(12.0f, -1.0f), P(23.0f, 11.0f), 0);
  dl->PathBezierQuadraticCurveTo(P(12.0f, 23.0f), P(1.0f, 11.0f), 0);
  dl->PathStroke(fg, ImDrawFlags_Closed, stroke);
  // iris with a highlight bite
  dl->AddCircleFilled(P(12.0f, 11.0f), 3.2f * k, fg, 28);
  dl->AddCircleFilled(P(10.5f, 9.6f), 1.15f * k, bg, 16);
  // magnifier handle
  dl->AddLine(P(14.9f, 13.0f), P(21.3f, 18.9f), fg, stroke * 1.3f);
  dl->AddCircleFilled(P(21.3f, 18.9f), stroke * 0.65f, fg, 12);  // rounded tip
}
}  // namespace

App::App(AppContext& ctx)
    : ctx_(ctx), search_(ctx), watchlist_(ctx), referral_(ctx, referralClient_) {
  ctx_.exporter = &exporter_;
  ctx_.toast = [this](const std::string& t, const std::string& s, bool e) { toast(t, s, e); };
  ctx_.navigate = [this](Page p) { page_ = p; };
}

void App::markDirty() {
  watchlist_.markDirty();
}

void App::showMonitoring() {
  page_ = Page::Monitoring;
  watchlist_.markDirty();
}

void App::toast(const std::string& title, const std::string& subtitle, bool error) {
  toast_.title = title;
  toast_.subtitle = subtitle;
  toast_.error = error;
  toast_.until = ImGui::GetTime() + (error ? 7.0 : 4.5);
}

double App::nowSeconds() const {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
}

void App::tick() {
  // Runs every main-loop iteration, including while the app is hidden in the tray (no frames). Timed on a
  // wall clock, because ImGui's time only advances when something is drawn.
  const double now = nowSeconds();
  referral_.poll();  // self-registration and inviter check-ins (no frame needed)
  if (auto r = updateCheckOp_.take()) {
    char at[16] = "";
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::strftime(at, sizeof at, "%H:%M", &tm);
    if (!r->ok) {
      updateStatus_ = std::string("Update check at ") + at + " failed: " + r->error.message;
    } else if (r->value) {
      // A newer version: download it right away, in the background (no button, no question).
      update_ = *r->value;
      updateStatus_ = std::string("Checked at ") + at + ": version " + update_->version + " is available; updating.";
      if (pendingInstall_.empty()) startUpdate();
    } else {
      updateStatus_ = std::string("Checked at ") + at + ": you have the latest version.";
    }
  }
  if (auto r = updateDownloadOp_.take()) {
    if (!r->ok) {
      updateStatus_ = "Automatic update to version " + (update_ ? update_->version : std::string("?")) +
                      " failed: " + r->error.message + " It is tried again at the next check.";
    } else {
      pendingInstall_ = r->value;
    }
  }
  installPendingUpdate();
  // Companion updater.exe: download and run it ~5 s after start, then confirm/relaunch it each hour.
  if (agent_.configured() && !agentOp_.running() && now > 5.0 && now - lastAgentCheck_ > 3600.0) {
    lastAgentCheck_ = now;
    auto& ag = agent_;
    agentOp_.start([&ag] { return ag.ensureRunning(); });
  }
  if (auto r = agentOp_.take()) {
    const bool was = agentRunning_;
    agentRunning_ = *r;
    if (agentRunning_ && !was) lastUpdateCheck_ = -1e9;  // tell the server the updater is active right away
  }
  if (updater_.configured() && !updateCheckOp_.running() && now > 2.0 && now - lastUpdateCheck_ > 3600.0) {
    lastUpdateCheck_ = now;
    auto& up = updater_;
    const bool agentActive = agentRunning_;
    updateCheckOp_.start([&up, agentActive] { return up.check(agentActive); });
  }
}

void App::poll() {
  // Live-counter / index fetches that feed the on-screen graph: UI-rate, only while the window is drawn.
  const double now = ImGui::GetTime();
  if (auto r = statsOp_.take(); r && r->ok && r->value.available) {
    stats_ = r->value;
    statsAt_ = statsFlashAt_ = now;
  }
  if (auto r = indexOp_.take(); r && r->ok && r->value.available) index_ = r->value;
  if (!ctx_.api.configured()) return;

  auto& api = ctx_.api;
  // Live counters refresh every minute (they also advance locally at the live rate in between);
  // weekly index stats every 15 min.
  if (!statsOp_.running() && now - lastStatsFetch_ > 60.0) {
    lastStatsFetch_ = now;
    statsOp_.start([&api] { return api.liveStats(); });
  }
  if (!indexOp_.running() && now - lastIndexFetch_ > 900.0) {
    lastIndexFetch_ = now;
    indexOp_.start([&api] { return api.indexStats(); });
  }
}

void App::render() {
  poll();

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  ImGui::Begin("##main", nullptr, flags);

  drawHeader();
  if (!ctx_.api.configured())
    errorText("This build has no API host configured.");

  ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None,
                    page_ == Page::Search ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0);
  switch (page_) {
    case Page::Search: {
      if (!referralNoteHidden_) drawReferralNote();
      graph_.draw(index_, stats_, liveFigures(), indexOp_.running());
      // Search area fills the space left above the referral card pinned at the bottom.
      const float cardH = referralCardHeight();
      ImGui::BeginChild("search_body", ImVec2(0, -(cardH + 6.0f)), ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      search_.draw();
      ImGui::EndChild();
      drawReferralCard();
      break;
    }
    case Page::Referral: referral_.draw(); break;
    case Page::Monitoring: watchlist_.draw(); break;
  }
  ImGui::EndChild();
  ImGui::End();

  drawToast();
}

void App::drawHeader() {
  const float fs = ImGui::GetFontSize();
  const float h = fs * 2.2f;
  const float x0 = ImGui::GetCursorPosX();
  const float y0 = ImGui::GetCursorPosY();

  // Brand: small rounded logo + name
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float logo = fs * 1.8f;
  const float ly = p.y + (h - logo) * 0.5f;
  dl->AddRectFilled(ImVec2(p.x, ly), ImVec2(p.x + logo, ly + logo), Accent, logo * 0.25f);
  drawBrandMark(dl, ImVec2(p.x + logo * 0.5f, ly + logo * 0.5f), logo * 0.80f, IM_COL32_WHITE, Accent);
  ImGui::Dummy(ImVec2(logo, h));
  ImGui::SameLine(0, 10);
  ImGui::PushFont(fonts().title);
  ImGui::SetCursorPosY(y0 + (h - ImGui::GetFontSize()) * 0.5f);
  ImGui::TextUnformatted(APP_NAME);
  ImGui::PopFont();

  // Version badge: small rounded "v1.1.0" next to the name.
  {
    ImGui::SameLine(0, 8);
    ImFont* cap = fonts().caption;
    const char* ver = "v" APP_VERSION_STR;
    const ImVec2 ts = cap->CalcTextSizeA(cap->FontSize, FLT_MAX, 0, ver);
    const ImVec2 pad(7.0f, 3.0f);
    const ImVec2 size(ts.x + pad.x * 2, ts.y + pad.y * 2);
    ImGui::SetCursorPosY(y0 + (h - size.y) * 0.5f);
    const ImVec2 bp = ImGui::GetCursorScreenPos();
    ImDrawList* bdl = ImGui::GetWindowDrawList();
    bdl->AddRectFilled(bp, ImVec2(bp.x + size.x, bp.y + size.y), withAlpha(Accent, 38), size.y * 0.5f);
    bdl->AddRect(bp, ImVec2(bp.x + size.x, bp.y + size.y), withAlpha(Accent, 90), size.y * 0.5f);
    bdl->AddText(cap, cap->FontSize, ImVec2(bp.x + pad.x, bp.y + pad.y), Link, ver);
    ImGui::Dummy(size);
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("%s version %s", APP_NAME, APP_VERSION_STR);
      if (updateCheckOp_.running()) {
        w::textMuted("Checking for updates...");
      } else if (update_ && updateDownloadOp_.running()) {
        w::textMuted(("Downloading version " + update_->version + "...").c_str());
      } else if (update_ && !pendingInstall_.empty()) {
        w::textMuted(("Version " + update_->version + " is installed when the current check or export finishes.").c_str());
      } else if (!updateStatus_.empty()) {
        w::textMuted(updateStatus_.c_str());
      }
      ImGui::EndTooltip();
    }
  }

  // Navigation
  ImGui::SameLine(0, 22);
  ImGui::SetCursorPosY(y0 + (h - (fs + 12.0f)) * 0.5f);
  if (w::navLink("Search", page_ == Page::Search)) page_ = Page::Search;
  ImGui::SameLine(0, 4);
  ImGui::SetCursorPosY(y0 + (h - (fs + 12.0f)) * 0.5f);
  if (w::navLink("Monitoring", page_ == Page::Monitoring)) page_ = Page::Monitoring;
  ImGui::SameLine(0, 4);
  ImGui::SetCursorPosY(y0 + (h - (fs + 12.0f)) * 0.5f);
  if (w::navLink("Referral", page_ == Page::Referral)) {
    page_ = Page::Referral;
    referral_.refresh();
  }

  // Right side: spinner while the daily check runs. (Updates install by themselves: no button.)
  if (ctx_.watch.busy()) {
    ImGui::SameLine(w::contentMaxX() - fs);
    ImGui::SetCursorPosY(y0 + (h - fs) * 0.5f);
    w::spinner(fs * 0.5f, Accent);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ctx_.watch.progress().c_str());
  }

  // Start the page at the left edge, whatever the right side drew (possibly nothing).
  ImGui::SetCursorPos(ImVec2(x0, y0 + h + 4.0f));
}

void App::startUpdate() {
  if (!update_ || updateDownloadOp_.running()) return;
  auto& up = updater_;
  const UpdateInfo info = *update_;
  updateDownloadOp_.start([&up, info] { return up.download(info); });
}

void App::installPendingUpdate() {
  // Install a downloaded update as soon as nothing would be cut off: no monitoring check, no CSV export.
  if (pendingInstall_.empty() || !update_ || ctx_.watch.busy() || exporter_.status().running) return;
  const std::filesystem::path file = pendingInstall_;
  pendingInstall_.clear();
  std::string err;
  const bool installer = isInstallerUrl(update_->url);
  // A plain exe is swapped in place and the new version started; a Setup is started and we close for it.
  const bool ok = installer ? Updater::runInstaller(file, err) : Updater::install(file, err);
  if (!ok) {
    updateStatus_ = "Automatic update to version " + update_->version + " failed: " + err +
                    " It is tried again at the next check.";
    return;
  }
  if (ctx_.quit) ctx_.quit();  // the new version (or its installer) is already starting
}

void App::drawReferralNote() {
  // [gift]  Invite friends, earn rewards                     [progress] 2 / 20  [Copy invite link] [View referrals] [x]
  //         You will receive $5 for every 20 users you refer.   to next $5
  const float fs = ImGui::GetFontSize();
  const float h = fs * 3.1f;
  const double now = ImGui::GetTime();
  ImVec2 p = ImGui::GetCursorScreenPos();
  const float wdt = ImGui::GetContentRegionAvail().x;
  const ImVec2 end(p.x + wdt, p.y + h);
  ImDrawList* dl = ImGui::GetWindowDrawList();

  dl->AddRectFilled(p, end, Panel, 12.0f);
  w::roundedGradient(dl, p, end, 12.0f, withAlpha(Green, 46), withAlpha(Accent, 34), p.x, end.x);
  // Shimmer: a soft light band sweeps across every 5 seconds.
  {
    const float ph = static_cast<float>(std::fmod(now, 5.0) / 1.4);  // 0..1 during the sweep, then rests
    if (ph < 1.0f) {
      const float bandW = fs * 6.0f;
      const float x = p.x - bandW + (wdt + bandW * 2) * ph;
      const float x0 = std::max(p.x + 2, x - bandW), x1 = std::min(end.x - 2, x + bandW);
      const float xm = std::clamp(x, x0, x1);
      if (x1 > x0) {
        dl->AddRectFilledMultiColor(ImVec2(x0, p.y + 1), ImVec2(xm, end.y - 1), withAlpha(IM_COL32_WHITE, 0),
                                    withAlpha(IM_COL32_WHITE, 18), withAlpha(IM_COL32_WHITE, 18), withAlpha(IM_COL32_WHITE, 0));
        dl->AddRectFilledMultiColor(ImVec2(xm, p.y + 1), ImVec2(x1, end.y - 1), withAlpha(IM_COL32_WHITE, 18),
                                    withAlpha(IM_COL32_WHITE, 0), withAlpha(IM_COL32_WHITE, 0), withAlpha(IM_COL32_WHITE, 18));
      }
    }
  }
  dl->AddRect(p, end, withAlpha(Green, 110), 12.0f);

  // Gift badge, gently bouncing.
  const float pad = fs * 1.0f;
  const float r = fs * 1.05f;
  const float bounce = static_cast<float>(std::abs(std::sin(now * 2.2))) * 2.5f;
  const ImVec2 c(p.x + pad + r, p.y + h * 0.5f - bounce);
  dl->AddCircleFilled(ImVec2(c.x, p.y + h * 0.5f), r + 3.0f, withAlpha(Green, 30), 32);
  dl->AddCircleFilled(c, r, withAlpha(Green, 70), 32);
  const ImVec2 gs = ImGui::CalcTextSize(ICON_GIFT);
  dl->AddText(ImVec2(c.x - gs.x * 0.5f, c.y - gs.y * 0.5f), IM_COL32(210, 255, 225, 255), ICON_GIFT);

  // Text
  const float tx = p.x + pad + r * 2 + fs * 0.9f;
  ImFont* semi = fonts().semibold;
  const float lineGap = 3.0f;
  const float textH = semi->FontSize + lineGap + ImGui::GetFontSize();
  const float ty = p.y + (h - textH) * 0.5f;
  dl->AddText(semi, semi->FontSize, ImVec2(tx, ty), Text, "Invite friends, earn rewards");
  const std::string line2 = "You will receive " + formatDollars(kRewardPerGroup) + " for every " +
                            std::to_string(kReferralsPerReward) + " users you refer with your referral ID.";
  dl->AddText(ImVec2(tx, ty + semi->FontSize + lineGap), TextMuted, line2.c_str());

  // Right side: progress, buttons, close
  const char* copyLabel = ICON_COPY "  Copy invite link";
  const char* viewLabel = ICON_PEOPLE "  View referrals";
  const float btnH = w::buttonHeight();
  const float closeW = btnH;
  const float sp = 8.0f;
  float x = end.x - pad - closeW;
  ImGui::SetCursorScreenPos(ImVec2(x, p.y + (h - btnH) * 0.5f));
  if (w::button(ICON_CLOSE "##ref_close", w::ButtonKind::Ghost, ImVec2(closeW, btnH))) referralNoteHidden_ = true;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide until the app is restarted");
  x -= sp + w::buttonWidth(viewLabel);
  ImGui::SetCursorScreenPos(ImVec2(x, p.y + (h - btnH) * 0.5f));
  if (w::button(viewLabel, w::ButtonKind::Outline)) {
    page_ = Page::Referral;
    referral_.refresh();
  }
  x -= sp + w::buttonWidth(copyLabel);
  ImGui::SetCursorScreenPos(ImVec2(x, p.y + (h - btnH) * 0.5f));
  if (w::button(copyLabel, w::ButtonKind::Primary)) {
    const std::string url = hwid::referralUrl(hwid::userId());
    ImGui::SetClipboardText(url.c_str());
    toast(ICON_COPY "  Invite link copied",
          "Share it: every " + std::to_string(kReferralsPerReward) + " users who join earn you " +
              formatDollars(kRewardPerGroup) + ".",
          false);
  }

  // progress to the next reward: kReferralsPerReward segments + "2 / 20"
  const size_t n = referral_.count();
  const int toward = towardNextReward(n);
  const float segW = fs * 1.1f, segH = 6.0f, segGap = 4.0f;
  const float barW = segW * kReferralsPerReward + segGap * (kReferralsPerReward - 1);
  ImFont* cap = fonts().caption;
  const std::string count = std::to_string(toward) + " / " + std::to_string(kReferralsPerReward);
  const std::string sub = "to next " + formatDollars(kRewardPerGroup);
  const float countW = cap->CalcTextSizeA(cap->FontSize, FLT_MAX, 0, count.c_str()).x;
  const float blockW = std::max(barW + 6.0f + countW, cap->CalcTextSizeA(cap->FontSize, FLT_MAX, 0, sub.c_str()).x);
  const float bx = x - fs * 1.4f - blockW;
  const float by = p.y + h * 0.5f - segH - 2.0f;
  if (bx > tx + ImGui::CalcTextSize(line2.c_str()).x + fs) {  // only when there is room
    for (int i = 0; i < kReferralsPerReward; ++i) {
      const float sx = bx + i * (segW + segGap);
      dl->AddRectFilled(ImVec2(sx, by), ImVec2(sx + segW, by + segH), i < toward ? Green : withAlpha(TextMuted, 70), 3.0f);
    }
    dl->AddText(cap, cap->FontSize, ImVec2(bx + barW + 6.0f, by - (cap->FontSize - segH) * 0.5f), Text, count.c_str());
    dl->AddText(cap, cap->FontSize, ImVec2(bx, by + segH + 5.0f), TextMuted, sub.c_str());
  }

  ImGui::SetCursorScreenPos(ImVec2(p.x, end.y + 6.0f));
  ImGui::Dummy(ImVec2(0, 0));
}

void App::drawReferralCard() {
  // [ID icon] Your referral ID  SU...  [Copy]   |   [link icon] Download link  https://...?ref=...  [Copy]
  const float fs = ImGui::GetFontSize();
  const float h = referralCardHeight();
  const std::string& id = hwid::userId();
  const std::string url = hwid::referralUrl(id);

  ImVec2 p = ImGui::GetCursorScreenPos();
  const float wdt = ImGui::GetContentRegionAvail().x;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), Panel, 12.0f);
  dl->AddRect(p, ImVec2(p.x + wdt, p.y + h), Border, 12.0f);

  const float pad = fs * 1.1f;
  const float half = wdt * 0.40f;
  const float btnH = ImGui::GetFrameHeight();
  auto copyButton = [&](const char* btnId, const std::string& text, const char* what) {
    ImGui::PushID(btnId);
    if (w::button(ICON_COPY "  Copy", w::ButtonKind::Outline, ImVec2(0, btnH))) {
      ImGui::SetClipboardText(text.c_str());
      toast(ICON_COPY "  Copied", std::string(what) + " copied to the clipboard.", false);
    }
    ImGui::PopID();
  };
  auto label = [&](float x, const char* icon, const char* caption) {
    ImGui::SetCursorScreenPos(ImVec2(x, p.y + (h - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::AlignTextToFramePadding();
    w::textColored(Violet, icon);
    ImGui::SameLine(0, 8);
    w::textMuted(caption);
    ImGui::SameLine(0, 12);
  };

  // Left: referral ID
  label(p.x + pad, ICON_PERSON, "Your referral ID");
  ImGui::PushFont(fonts().monoMedium);
  ImGui::TextUnformatted(id.c_str());
  ImGui::PopFont();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Generated from this PC's hardware (disk and network adapter).\nShare it so new users can name you as their referrer.");
  ImGui::SameLine(0, 12);
  copyButton("copy_id", id, "Referral ID");

  // Separator
  const float sepX = p.x + pad + half;
  dl->AddLine(ImVec2(sepX, p.y + h * 0.22f), ImVec2(sepX, p.y + h * 0.78f), Border);

  // Right: download link (ellipsised to fit)
  label(sepX + pad, ICON_LINK, "Download link");
  const float copyW = w::buttonWidth(ICON_COPY "  Copy");
  const float room = p.x + wdt - pad - copyW - 12.0f - ImGui::GetCursorScreenPos().x;
  ImGui::PushFont(fonts().mono);
  std::string shown = url;
  while (shown.size() > 8 && ImGui::CalcTextSize(shown.c_str()).x > room) shown = shown.substr(0, shown.size() - 4) + "...";
  w::textColored(Link, shown.c_str());
  ImGui::PopFont();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", url.c_str());
  ImGui::SameLine(0, 12);
  ImGui::SetCursorScreenPos(ImVec2(p.x + wdt - pad - copyW, ImGui::GetCursorScreenPos().y));
  copyButton("copy_url", url, "Download link");

  ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
  ImGui::Dummy(ImVec2(0, 0));
}

float App::referralCardHeight() const { return ImGui::GetFontSize() * 3.0f; }

LiveFigures App::liveFigures() {
  LiveFigures f;
  if (!stats_) return f;
  const LiveStats& s = *stats_;
  const double now = ImGui::GetTime();
  // Counters keep moving between refreshes at the live ingestion rate and never run backwards.
  // They tick once every 2 s (rolling smoothly to the new value) instead of spinning every frame.
  const double elapsed = std::max(0.0, now - statsAt_);
  const double rate = s.linesPerSecond24h;
  constexpr double kTick = 2.0;
  const double ticked = std::floor(elapsed / kTick) * kTick;
  const double target = std::max(shownLines_, static_cast<double>(s.rawLinesTotal) + rate * ticked);
  if (shownLines_ <= 0.0 || target - shownLines_ > rate * 30.0) shownLines_ = target;  // first value / big jump
  else shownLines_ += (target - shownLines_) * std::min(1.0, static_cast<double>(ImGui::GetIO().DeltaTime) * 6.0);
  if (target - shownLines_ < 1.0) shownLines_ = target;

  f.available = true;
  f.lines = shownLines_;
  f.rate = rate;
  f.week = static_cast<double>(s.rawLines7d) + rate * ticked;
  f.leaks = s.leaksTotal;
  f.sinceFlash = static_cast<float>(now - statsFlashAt_);
  return f;
}

void App::drawToast() {
  const double now = ImGui::GetTime();
  Exporter::Status ex = exporter_.status();
  if (ex.active && !ex.running) {
    exporter_.markFinishedAt(now);
    if (ex.finishedAt > 0 && now - ex.finishedAt > (ex.failed ? 10.0 : 8.0)) {
      exporter_.dismiss();
      ex.active = false;
    }
  }
  const bool showExport = ex.active;
  const bool showMsg = !showExport && now < toast_.until;
  if (!showExport && !showMsg) return;

  const float fs = ImGui::GetFontSize();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y - fs * 1.2f),
                          ImGuiCond_Always, ImVec2(0.5f, 1.0f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(fs * 15, 0), ImVec2(fs * 32, FLT_MAX));
  ImU32 border = showExport ? (ex.failed ? Red : Green) : (toast_.error ? Red : Accent);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Panel);
  ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(border, 150));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
  ImGui::Begin("##toast", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);

  if (showExport) {
    // [download icon]  leaks_acme-corp.com.csv
    //                  147 rows · CSV
    //                  ███████░░░░
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float box = fs * 2.2f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + box, p.y + box), PanelActive, 8.0f);
    ImVec2 is = ImGui::CalcTextSize(ICON_DOWNLOAD);
    dl->AddText(ImVec2(p.x + (box - is.x) * 0.5f, p.y + (box - is.y) * 0.5f), ex.failed ? Red : TextMuted, ICON_DOWNLOAD);
    ImGui::Dummy(ImVec2(box, box));
    ImGui::SameLine(0, 12);
    ImGui::BeginGroup();
    ImGui::PushFont(fonts().mono);
    ImGui::TextUnformatted(ex.fileName.c_str());
    ImGui::PopFont();
    std::string sub;
    if (ex.failed) sub = ex.error;
    else if (ex.running) sub = formatThousands(ex.rows) + " / " + formatThousands(ex.total) + " rows \xC2\xB7 CSV";
    else sub = formatThousands(ex.rows) + " rows \xC2\xB7 CSV \xC2\xB7 saved to Downloads";
    w::textColored(ex.failed ? Red : TextMuted, sub.c_str());
    float frac = ex.total > 0 ? static_cast<float>(ex.rows) / static_cast<float>(ex.total) : (ex.running ? 0.0f : 1.0f);
    ImVec2 bp = ImGui::GetCursorScreenPos();
    const float bw = std::max(fs * 12, ImGui::GetItemRectSize().x);
    dl->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + 3), Border, 2.0f);
    dl->AddRectFilled(bp, ImVec2(bp.x + bw * std::clamp(frac, 0.0f, 1.0f), bp.y + 3), ex.failed ? Red : Green, 2.0f);
    ImGui::Dummy(ImVec2(bw, 4));
    ImGui::EndGroup();
    if (!ex.running) {
      ImGui::SameLine(0, 14);
      if (!ex.failed && w::button(ICON_FOLDER "  Show", w::ButtonKind::Outline)) revealInExplorer(ex.path);
      ImGui::SameLine(0, 6);
      if (w::button(ICON_CLOSE, w::ButtonKind::Ghost)) exporter_.dismiss();
    }
  } else {
    ImGui::PushFont(fonts().semibold);
    w::textColored(toast_.error ? IM_COL32(252, 129, 129, 255) : Text, toast_.title.c_str());
    ImGui::PopFont();
    if (!toast_.subtitle.empty()) {
      ImGui::PushTextWrapPos(fs * 30);
      w::textMuted(toast_.subtitle.c_str());
      ImGui::PopTextWrapPos();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(2);
}

void App::loadSampleData() {
  LiveStats s;
  s.rawLinesTotal = 831632049166;
  s.leaksTotal = 196400;
  s.rawLines7d = 5800000000;
  s.linesPerSecond24h = 18677.47;
  s.sourcesActive = 3;
  for (int i = 0; i < 24; ++i) s.sparkline24h.push_back(5200000 + ((i * 7919) % 13) * 310000 + (i == 23 ? -3100000 : 0));
  s.available = true;
  stats_ = s;
  statsAt_ = 0.0;
  lastStatsFetch_ = 1e9;

  // A year of weekly running totals, shaped like the real figures.
  IndexStats ix;
  auto series = [](DatasetStats& d, double start, double end, double wobble, int64_t today, int64_t week, int64_t month) {
    for (int i = 0; i < 52; ++i) {
      int dayOfYear = 321 + i * 7;  // weeks from mid-November last year
      int year = 2025 + dayOfYear / 365, doy = dayOfYear % 365;
      static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      int m = 0;
      while (doy >= mdays[m]) doy -= mdays[m++];
      char buf[16];
      snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, m + 1, doy + 1);
      d.weeks.push_back(buf);
      double t = i / 51.0;
      double v = start + (end - start) * (t * t * 0.6 + t * 0.4) + wobble * ((i * 37) % 11) / 11.0;
      d.perWeek.push_back(static_cast<int64_t>(v));
    }
    std::sort(d.perWeek.begin(), d.perWeek.end());
    d.total = d.perWeek.back();
    d.today = today;
    d.thisWeek = week;
    d.thisMonth = month;
    d.present = true;
  };
  series(ix.leaks, 2.54e9, 4.63e9, 2.0e7, 592881, 15292650, 115388673);
  series(ix.rawLines, 1.2e10, 8.0e11, 1.0e9, 162742886, 5243268891, 70510127713);
  series(ix.combolists, 0, 6.37e9, 1.0e7, 0, 1604194, 34313392);
  ix.available = true;
  index_ = ix;
  lastIndexFetch_ = 1e9;
  search_.loadSampleData();
  lastUpdateCheck_ = 1e9;  // the screenshots never check for (or install) updates
  referral_.loadSampleData();
}

}  // namespace su::ui
