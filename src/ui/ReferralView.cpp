#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "AppInfo.h"
#include "core/DownloadStamp.h"
#include "core/HardwareId.h"
#include "core/Util.h"
#include "ui/Theme.h"
#include "ui/Views.h"
#include "ui/Widgets.h"

namespace su::ui {

using namespace color;

namespace {

// Rounded stat card: [icon]  caption / big value / sub text, optional progress segments.
void statCard(const char* id, const char* icon, ImU32 col, const char* caption, const std::string& value,
              const std::string& sub, float width, int segments = 0, int filled = 0) {
  const float fs = ImGui::GetFontSize();
  const float h = fs * 6.0f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), Panel, 12.0f);
  w::roundedGradient(dl, p, ImVec2(p.x + width, p.y + h), 12.0f, withAlpha(col, 28), withAlpha(col, 0), p.x,
                     p.x + width * 0.6f);
  dl->AddRect(p, ImVec2(p.x + width, p.y + h), Border, 12.0f);

  const float pad = fs * 1.1f;
  const float box = fs * 2.2f;
  const ImVec2 b0(p.x + pad, p.y + pad);
  dl->AddRectFilled(b0, ImVec2(b0.x + box, b0.y + box), withAlpha(col, 40), 10.0f);
  const ImVec2 is = ImGui::CalcTextSize(icon);
  dl->AddText(ImVec2(b0.x + (box - is.x) * 0.5f, b0.y + (box - is.y) * 0.5f), col, icon);

  const float tx = b0.x + box + fs * 0.9f;
  ImFont* cap = fonts().caption;
  dl->AddText(cap, cap->FontSize, ImVec2(tx, p.y + pad), TextMuted, caption);
  ImFont* big = fonts().monoBig;
  dl->AddText(big, big->FontSize, ImVec2(tx, p.y + pad + cap->FontSize + 4), Text, value.c_str());
  const float subY = p.y + pad + cap->FontSize + big->FontSize + 10;
  if (segments > 0) {
    const float sw = (p.x + width - pad - tx - (segments - 1) * 5.0f) / segments;
    for (int i = 0; i < segments; ++i) {
      const float x0 = tx + i * (sw + 5.0f);
      dl->AddRectFilled(ImVec2(x0, subY), ImVec2(x0 + sw, subY + 6), i < filled ? col : withAlpha(Border, 255), 3.0f);
    }
    dl->AddText(cap, cap->FontSize, ImVec2(tx, subY + 11), TextMuted, sub.c_str());
  } else {
    dl->AddText(cap, cap->FontSize, ImVec2(tx, subY), TextMuted, sub.c_str());
  }
  ImGui::PushID(id);
  ImGui::Dummy(ImVec2(width, h));
  ImGui::PopID();
}

}  // namespace

constexpr const char* kInvitedBySetting = "invited_by";
constexpr const char* kInvitedSourceSetting = "invited_by_source";          // "download" when stamped into the exe
constexpr const char* kInvitedRegisteredSetting = "invited_by_registered";  // "1" once the server has it
constexpr const char* kSelfRegisteredSetting = "self_registered";  // the app version that registered our ID

ReferralView::ReferralView(AppContext& ctx, ReferralClient& client) : ctx_(ctx), client_(client) {
  // Register our own referral ID with the server on first run, and again after each app update: we store the
  // version that registered and re-register when it differs, so a PC whose earlier registration never landed
  // (offline, or an older broken build) self-heals on the next version. Re-registering is idempotent (409).
  selfRegistered_ = ctx_.db.getSetting(kSelfRegisteredSetting) == APP_VERSION_STR;
  selfRegPending_ = !selfRegistered_;

  invitedBy_ = ctx_.db.getSetting(kInvitedBySetting);
  if (invitedBy_.empty()) {
    // First run from a referral download link: the inviter is stamped into the exe. Save it at once (so it
    // is locked before the user can type anything) and register it with the server in the background.
    if (auto stamped = downloadstamp::readInviter(); stamped && *stamped != hwid::userId()) {
      invitedBy_ = *stamped;
      ctx_.db.setSetting(kInvitedBySetting, invitedBy_);
      ctx_.db.setSetting(kInvitedSourceSetting, "download");
      ctx_.db.setSetting(kInvitedRegisteredSetting, "0");
      invitedFromDownload_ = true;
      stampRegisterPending_ = true;
    }
  } else {
    invitedFromDownload_ = ctx_.db.getSetting(kInvitedSourceSetting) == "download";
    // A stamped inviter that never reached the server (offline first run) is retried on later launches.
    if (invitedFromDownload_ && ctx_.db.getSetting(kInvitedRegisteredSetting) != "1") stampRegisterPending_ = true;
  }
}

void ReferralView::refresh() {
  if (fetchOp_.running()) return;
  lastFetch_ = ImGui::GetTime();
  auto& client = client_;
  const std::string id = hwid::userId();
  fetchOp_.start([&client, id] { return client.fetch(id); });
}

void ReferralView::poll() {
  // Register our own referral ID with the server, once. Only when a referral API is configured, so a
  // build without one never marks this PC as registered (which would stop a later build from retrying).
  if (selfRegPending_ && client_.configured() && !selfRegOp_.running()) {
    selfRegPending_ = false;
    auto& client = client_;
    const std::string me = hwid::userId();
    selfRegOp_.start([&client, me] { return client.registerSelf(me); });
  }
  if (auto r = selfRegOp_.take()) {
    if (r->ok) {
      selfRegistered_ = true;
      ctx_.db.setSetting(kSelfRegisteredSetting, APP_VERSION_STR);  // remember which version registered
      refresh();  // now that the server knows this app, load the real figures
    }  // on failure, selfRegPending_ stays false this run and is retried on the next launch
  }

  // Register a stamped inviter with the referral server (silently; it is already saved locally).
  // Wait until our own ID is registered so the server knows this app.
  if (stampRegisterPending_ && selfRegistered_ && !inviteOp_.running()) {
    stampRegisterPending_ = false;
    silentInvite_ = true;
    pendingInviter_ = invitedBy_;
    auto& client = client_;
    const std::string me = hwid::userId(), inviter = invitedBy_;
    inviteOp_.start([&client, me, inviter] { return client.registerInviter(me, inviter); });
  }
  if (auto r = inviteOp_.take()) {
    if (r->ok) {
      invitedBy_ = pendingInviter_;
      ctx_.db.setSetting(kInvitedBySetting, invitedBy_);
      if (silentInvite_) {
        ctx_.db.setSetting(kInvitedRegisteredSetting, "1");
      } else {
        invitedInput_.clear();
        ctx_.notify(ICON_CHECK "  Inviter saved", invitedBy_ + " is saved as the user who invited you.");
      }
    } else if (silentInvite_) {
      // 409 = the server already has this inviter for this PC: done, no retry needed.
      if (r->error.status == 409) ctx_.db.setSetting(kInvitedRegisteredSetting, "1");
    } else {
      ctx_.notify(ICON_WARNING "  Inviter not saved", r->error.message, true);
    }
    silentInvite_ = false;
    pendingInviter_.clear();
  }
  if (auto r = fetchOp_.take()) {
    if (r->ok) {
      status_ = r->value;
      error_.clear();
    } else {
      error_ = r->error.message;
    }
  }
  // Keep the figures fresh (every 10 minutes) while the app is open.
  if (!fetchOp_.running() && ImGui::GetTime() - lastFetch_ > 600.0) refresh();
}

size_t ReferralView::count() const { return status_ ? status_->referrals.size() : 0; }

void ReferralView::draw() {
  const float fs = ImGui::GetFontSize();
  const float sp = ImGui::GetStyle().ItemSpacing.x;
  const std::string& myId = hwid::userId();
  const std::string link = hwid::referralUrl(myId);

  // ---- title row
  w::heading("Referrals");
  w::textMuted(("Share your referral ID. You receive " + formatDollars(kRewardPerGroup) + " for every " +
                std::to_string(kReferralsPerReward) + " users who join with it.")
                   .c_str());
  {
    const char* label = ICON_REFRESH "  Refresh";
    ImGui::SameLine(w::contentMaxX() - w::buttonWidth(label));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - fs * 1.2f);
    if (w::button(label, w::ButtonKind::Outline, ImVec2(0, 0), !fetchOp_.running())) refresh();
  }
  ImGui::Spacing();

  // ---- three stat cards
  const size_t n = count();
  const int toward = towardNextReward(n);
  const float cw = (ImGui::GetContentRegionAvail().x - sp * 2) / 3.0f;
  statCard("users", ICON_PEOPLE, Blue, "REFERRED USERS", formatThousands(static_cast<int64_t>(n)),
           n == 1 ? "user joined with your ID" : "users joined with your ID", cw);
  ImGui::SameLine();
  statCard("reward", ICON_GIFT, Green, "REWARD EARNED", formatDollars(rewardDollars(n)),
           formatThousands(rewardGroups(n)) + " x " + formatDollars(kRewardPerGroup) + " (every " +
               std::to_string(kReferralsPerReward) + " users)",
           cw);
  ImGui::SameLine();
  statCard("next", ICON_STAR, Violet, "NEXT $5", std::to_string(toward) + " / " + std::to_string(kReferralsPerReward),
           std::to_string(kReferralsPerReward - toward) + " more to earn " + formatDollars(kRewardPerGroup), cw,
           kReferralsPerReward, toward);

  // ---- your ID + link
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, Panel);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
  ImGui::BeginChild("share", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                                               ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::AlignTextToFramePadding();
  w::textColored(Violet, ICON_PERSON);
  ImGui::SameLine(0, 8);
  w::textMuted("Your referral ID");
  ImGui::SameLine(0, 12);
  ImGui::PushFont(fonts().monoMedium);
  ImGui::TextUnformatted(myId.c_str());
  ImGui::PopFont();
  ImGui::SameLine(0, 12);
  if (w::button(ICON_COPY "  Copy ID", w::ButtonKind::Outline)) {
    ImGui::SetClipboardText(myId.c_str());
    ctx_.notify(ICON_COPY "  Copied", "Referral ID copied to the clipboard.");
  }
  ImGui::SameLine(0, fs * 2);
  w::textColored(Violet, ICON_LINK);
  ImGui::SameLine(0, 8);
  ImGui::PushFont(fonts().mono);
  w::textColored(Link, link.c_str());
  ImGui::PopFont();
  ImGui::SameLine(0, 12);
  if (w::button(ICON_COPY "  Copy invite link", w::ButtonKind::Primary)) {
    ImGui::SetClipboardText(link.c_str());
    ctx_.notify(ICON_COPY "  Copied", "Invite link copied. Share it with friends.");
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  drawInvitedBy();

  // ---- referred users table
  ImGui::Spacing();
  if (!error_.empty()) errorText(error_);
  if (fetchOp_.running() && !status_) spinner("Loading referrals...");
  if (status_ && status_->referrals.empty()) {
    mutedText(("No referred users yet. Share your invite link: every " + std::to_string(kReferralsPerReward) +
               " users who join earn you " + formatDollars(kRewardPerGroup) + ".")
                  .c_str());
  }
  if (!status_ || status_->referrals.empty()) return;

  // ---- invited users: their referral IDs
  const auto& rows = status_->referrals;
  ImGui::PushFont(fonts().semibold);
  ImGui::TextUnformatted("Invited users");
  ImGui::PopFont();
  tableBegin();
  const bool open = ImGui::BeginTable("referrals", 2, tableFlags(), ImVec2(0, tableFitHeight(rows.size())));
  if (open) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, fs * 3);
    ImGui::TableSetupColumn("REFERRAL ID", ImGuiTableColumnFlags_WidthStretch);
    ImGui::PushFont(fonts().caption);
    ImGui::PushStyleColor(ImGuiCol_Text, TextMuted);
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    const float rh = tableRowHeight();
    for (size_t i = 0; i < rows.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow(ImGuiTableRowFlags_None, rh);
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      w::textMuted(std::to_string(i + 1).c_str());
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::PushFont(fonts().mono);
      ImGui::TextUnformatted(rows[i].id.c_str());
      ImGui::PopFont();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  tableEnd(open);
}

void ReferralView::drawInvitedBy() {
  // [person] Invited by  [SUXXXXXXXXXXX]  [Save]   hint / status
  const float fs = ImGui::GetFontSize();
  const std::string& myId = hwid::userId();
  const bool saved = !invitedBy_.empty();
  const bool sending = inviteOp_.running();

  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  w::textColored(Violet, ICON_PERSON);
  ImGui::SameLine(0, 8);
  w::textMuted("Invited by");
  ImGui::SameLine(0, 12);

  std::string shown = saved ? invitedBy_ : (sending ? pendingInviter_ : invitedInput_);
  ImGui::BeginDisabled(saved || sending);
  ImGui::SetNextItemWidth(fs * 15);
  ImGui::PushFont(fonts().mono);
  if (ImGui::InputTextWithHint("##invited_by", "SUXXXXXXXXXXX", saved || sending ? &shown : &invitedInput_,
                               ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank)) {
    if (invitedInput_.size() > 16) invitedInput_.resize(16);
    // Your own ID is not allowed in this box: typed or pasted (in any spelling), it is cleared at once.
    if (normalizeReferralId(invitedInput_) == myId) {
      invitedInput_.clear();
      if (ImGuiInputTextState* st = ImGui::GetInputTextState(ImGui::GetItemID())) st->ReloadUserBufAndSelectAll();
      ctx_.notify(ICON_WARNING "  Own referral ID", "You can't use your own referral ID. Enter the ID of the person who invited you.",
                  true);
    }
  }
  ImGui::PopFont();
  ImGui::EndDisabled();

  const std::string candidate = normalizeReferralId(invitedInput_);
  const bool hasInput = !trim(invitedInput_).empty() && normalizeReferralId(invitedInput_) != myId;
  ImGui::SameLine();
  ImGui::BeginDisabled(saved || sending);
  const char* label = saved ? ICON_CHECK "  Saved" : sending ? "Saving..." : ICON_CHECK "  Save";
  if (w::button(label, w::ButtonKind::Primary, ImVec2(0, 0), !saved && !sending && hasInput)) {
    // The checksum (last character) catches typing mistakes before anything is saved.
    const std::string problem = referralIdProblem(invitedInput_, myId);
    if (!problem.empty()) {
      ctx_.notify(ICON_WARNING "  Invalid referral ID", problem, true);
    } else {
      invitedInput_ = candidate;
      pendingInviter_ = candidate;
      openInviteConfirm_ = true;
    }
  }
  ImGui::EndDisabled();

  ImGui::SameLine(0, 14);
  ImGui::AlignTextToFramePadding();
  if (saved && invitedFromDownload_) {
    w::textMuted("Set from your download link and cannot be changed.");
  } else if (saved) {
    w::textMuted("The inviter can be set only once.");
  } else {
    w::textMuted("Enter the referral ID of the person who invited you. It can be saved once.");
  }

  // ---- confirmation: the choice is final
  if (openInviteConfirm_) {
    ImGui::OpenPopup("##invite_confirm");
    openInviteConfirm_ = false;
  }
  ImGui::SetNextWindowSize(ImVec2(fs * 25, 0));
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 22));
  if (ImGui::BeginPopupModal("##invite_confirm", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                               ImGuiWindowFlags_AlwaysAutoResize)) {
    w::heading(ICON_PERSON "  Save inviter?");
    ImGui::Spacing();
    ImGui::PushFont(fonts().monoMedium);
    ImGui::TextUnformatted(pendingInviter_.c_str());
    ImGui::PopFont();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0);
    w::textMuted("This ID will be saved as the user who invited you. It can't be changed later.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const char* ok = ICON_CHECK "  Save";
    const float ow = w::buttonWidth(ok), cw = w::buttonWidth("Cancel");
    ImGui::SetCursorPosX(w::contentMaxX() - ow - cw - ImGui::GetStyle().ItemSpacing.x);
    if (w::button("Cancel", w::ButtonKind::Outline)) {
      pendingInviter_.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (w::button(ok, w::ButtonKind::Primary)) {
      auto& client = client_;
      const std::string me = myId, inviter = pendingInviter_;
      inviteOp_.start([&client, me, inviter] { return client.registerInviter(me, inviter); });
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleVar();
}

void ReferralView::loadSampleData() {
  status_ = mockReferrals();
  lastFetch_ = 1e9;
}

}  // namespace su::ui
