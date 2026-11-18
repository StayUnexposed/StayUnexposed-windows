#include <imgui.h>
#include <imgui_stdlib.h>

#include <cstdlib>

#include "core/Autostart.h"
#include "core/Util.h"
#include "ui/Theme.h"
#include "ui/Views.h"
#include "ui/Widgets.h"

namespace su::ui {

using namespace color;

namespace {
std::string totalText(int64_t total) {
  if (total == kTotalUnknown) return "-";
  if (total == kTotalUnavailable) return "n/a";
  return formatThousands(total);
}

ImU32 kindColor(WatchKind k) {
  switch (k) {
    case WatchKind::Email: return Green;
    case WatchKind::Username: return Blue;
    case WatchKind::Domain: return Violet;
  }
  return TextMuted;
}
}  // namespace

WatchlistView::WatchlistView(AppContext& ctx) : ctx_(ctx) {
  checkTime_ = ctx_.db.getSetting(kSettingCheckTime, "08:00");
  minimizeToTray_ = ctx_.db.getSetting(kSettingMinimizeToTray, "1") == "1";
  autostart_ = autostart::isEnabled();
}

void WatchlistView::reload() {
  items_ = ctx_.db.listWatches();
  if (selected_) selectedLeaks_ = ctx_.db.watchLeaks(selected_);
}

void WatchlistView::draw() {
  if (dirty_.exchange(false)) reload();
  const float fs = ImGui::GetFontSize();

  w::heading("Monitoring");
  w::textMuted("Saved emails, usernames and domains are checked every day. New leaks raise a desktop alert.");
  ImGui::Spacing();

  // ---- Add ----
  static const char* kinds[] = {"Email or username", "Email", "Username", "Domain"};
  const float h = fs * 2.75f;
  const char* addLabel = ICON_ADD "  Add";
  const float addW = w::buttonWidth(addLabel) + fs;
  const float comboW = fs * 11;
  const float sp = ImGui::GetStyle().ItemSpacing.x;
  bool submit = w::searchBar("watch_new", "Email, username or domain to monitor", newValue_,
                             ImGui::GetContentRegionAvail().x - addW - comboW - sp * 2, h);
  ImGui::SameLine();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, (h - fs) * 0.5f));
  ImGui::SetNextItemWidth(comboW);
  ImGui::Combo("##kind", &newKind_, kinds, IM_ARRAYSIZE(kinds));
  ImGui::PopStyleVar();
  ImGui::SameLine();
  submit |= w::button(addLabel, w::ButtonKind::Primary, ImVec2(addW, h), !trim(newValue_).empty());
  if (submit && !trim(newValue_).empty()) {
    std::string value = trim(newValue_);
    WatchKind kind = newKind_ == 1   ? WatchKind::Email
                     : newKind_ == 2 ? WatchKind::Username
                     : newKind_ == 3 ? WatchKind::Domain
                                     : (looksLikeEmail(value) ? WatchKind::Email : WatchKind::Username);
    if (kind == WatchKind::Domain)
      for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (int64_t id = ctx_.db.addWatch(value, kind)) {
      ctx_.watch.requestCheck(id);
      newValue_.clear();
      addError_.clear();
      dirty_ = true;
    } else {
      addError_ = "That value is already monitored.";
    }
  }
  if (!addError_.empty()) errorText(addError_);

  // ---- Schedule settings ----
  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  w::textMuted(ICON_CLOCK "  Daily check at");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(fs * 4.5f);
  ImGui::PushFont(fonts().mono);
  bool timeEdited = ImGui::InputTextWithHint("##time", "HH:MM", &checkTime_, ImGuiInputTextFlags_EnterReturnsTrue);
  timeEdited |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::PopFont();
  if (timeEdited) {
    int minutes;
    if (parseHHMM(checkTime_, minutes)) {
      checkTime_ = formatHHMM(minutes);
      ctx_.db.setSetting(kSettingCheckTime, checkTime_);
      checkTimeError_.clear();
    } else {
      checkTimeError_ = "Use 24-hour HH:MM, e.g. 08:00";
    }
  }
  ImGui::SameLine(0, 24);
  if (ImGui::Checkbox("Start with Windows", &autostart_))
    if (!autostart::setEnabled(autostart_)) autostart_ = autostart::isEnabled();
  ImGui::SameLine(0, 24);
  if (ImGui::Checkbox("Keep running in the tray when closed", &minimizeToTray_))
    ctx_.db.setSetting(kSettingMinimizeToTray, minimizeToTray_ ? "1" : "0");
  ImGui::SameLine();
  helpMarker("Daily checks only run while the app is running. Keep it in the tray and start it with Windows "
             "so no day is missed. A missed check runs as soon as the app starts again.");

  // right side: check all + progress
  const char* checkLabel = ICON_REFRESH "  Check all now";
  ImGui::SameLine(w::contentMaxX() - w::buttonWidth(checkLabel));
  if (w::button(checkLabel, w::ButtonKind::Outline, ImVec2(0, 0), !items_.empty() && !ctx_.watch.busy()))
    ctx_.watch.requestCheckAll();
  if (!checkTimeError_.empty()) errorText(checkTimeError_);

  std::string progress = ctx_.watch.progress();
  if (!progress.empty()) spinner(progress.c_str());
  else {
    int64_t last = std::atoll(ctx_.db.getSetting(kSettingLastDailyRun, "0").c_str());
    w::textMuted(("Last daily run: " + formatLocalTime(last)).c_str());
  }
  ImGui::Spacing();

  if (items_.empty()) {
    mutedText("Nothing is monitored yet. Add a value above, or use \"Add Monitoring\" on a search result.");
    return;
  }

  // ---- Monitored values ----
  float tableHeight = tableFitHeight(items_.size());
  if (selected_) tableHeight = std::min(tableHeight > 0 ? tableHeight : 1e9f, ImGui::GetContentRegionAvail().y * 0.42f);
  tableBegin();
  bool drawn = ImGui::BeginTable("watch", 7, tableFlags(), ImVec2(0, tableHeight));
  if (drawn) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("VALUE", ImGuiTableColumnFlags_WidthStretch, 2.8f);
    ImGui::TableSetupColumn("TYPE", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("LEAKS", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("LAST CHECK", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("STATUS", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("NEW", ImGuiTableColumnFlags_WidthStretch, 0.5f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::PushFont(fonts().caption);
    ImGui::PushStyleColor(ImGuiCol_Text, TextMuted);
    ImGui::TableHeadersRow();
    ImGui::PopStyleColor();
    ImGui::PopFont();

    const float rowHeight = tableRowHeight();
    for (const auto& item : items_) {
      ImGui::PushID(static_cast<int>(item.id));
      ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::PushFont(fonts().mono);
      if (ImGui::Selectable(item.value.c_str(), selected_ == item.id,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                            ImVec2(0, rowHeight - ImGui::GetStyle().CellPadding.y * 2))) {
        selected_ = selected_ == item.id ? 0 : item.id;
        selectedLeaks_ = selected_ ? ctx_.db.watchLeaks(selected_) : std::vector<LeakRow>{};
      }
      ImGui::PopFont();
      ImGui::TableNextColumn();
      w::badge(watchKindLabel(item.kind), kindColor(item.kind));
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(totalText(item.stealerTotal).c_str());
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      w::textMuted(formatLocalTime(item.lastCheckAt).c_str());
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      if (!item.lastStatus.empty()) {
        w::textColored(Orange, (std::string(ICON_WARNING " ") + item.lastStatus).c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s\nIt will be retried on the next run.", item.lastStatus.c_str());
      } else if (!item.baselineDone) {
        w::textMuted("Waiting for first check");
      } else {
        w::textColored(Green, ICON_CHECK "  OK");
      }
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      if (item.unreadNew > 0) w::textColored(Red, ("+" + formatThousands(item.unreadNew)).c_str());
      else w::textMuted("0");
      ImGui::TableNextColumn();
      const float ib = ImGui::GetFrameHeight();
      if (w::iconButton("##check", ICON_REFRESH, "Check now", 0, ib) && !ctx_.watch.busy())
        ctx_.watch.requestCheck(item.id);
      ImGui::SameLine(0, 4);
      if (confirmRemove_ == item.id) {
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(Red, 200));
        if (ImGui::SmallButton("Remove")) {
          ctx_.db.removeWatch(item.id);
          if (selected_ == item.id) {
            selected_ = 0;
            selectedLeaks_.clear();
          }
          confirmRemove_ = 0;
          dirty_ = true;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) confirmRemove_ = 0;
      } else {
        if (w::iconButton("##remove", ICON_DELETE, "Stop monitoring", 0, ib)) confirmRemove_ = item.id;
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  tableEnd(drawn);

  if (selected_) {
    ImGui::Spacing();
    ImGui::PushFont(fonts().semibold);
    ImGui::Text("Known leaks (%s)", formatThousands(static_cast<int64_t>(selectedLeaks_.size())).c_str());
    ImGui::PopFont();
    TableOptions opt;
    opt.infoLabel = "SOURCE";
    drawResultsTable("watch_leaks", selectedLeaks_, opt);
  }
}

}  // namespace su::ui
