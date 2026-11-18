#include "ui/ResultsTable.h"

#include <imgui.h>

#include <algorithm>
#include <string>

#include "core/Util.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace su::ui {

using namespace color;

namespace {

const char* orDash(const std::string& s) { return s.empty() ? "-" : s.c_str(); }

constexpr const char* kDots = "●●●●●●●●";  // 8 x U+25CF

void copyRow(const LeakRow& r) {
  std::string text = r.url + "\t" + r.username + "\t" + r.password + "\t" + shortDate(r.addedAt);
  ImGui::SetClipboardText(text.c_str());
}

void rowContextMenu(const char* popupId, const LeakRow& r) {
  if (ImGui::BeginPopupContextItem(popupId)) {
    if (!r.url.empty() && ImGui::MenuItem(ICON_COPY "  Copy URL")) ImGui::SetClipboardText(r.url.c_str());
    if (ImGui::MenuItem(ICON_COPY "  Copy username")) ImGui::SetClipboardText(r.username.c_str());
    if (!r.password.empty() && ImGui::MenuItem(ICON_COPY "  Copy password"))
      ImGui::SetClipboardText(r.password.c_str());
    if (ImGui::MenuItem(ICON_COPY "  Copy row")) copyRow(r);
    if (!r.url.empty()) {
      ImGui::Separator();
      if (ImGui::MenuItem(ICON_LINK "  Open URL")) openUrl(r.url);
    }
    ImGui::EndPopup();
  }
}

}  // namespace

std::unordered_map<std::string, double>& revealedRows() {
  static std::unordered_map<std::string, double> rows;
  return rows;
}

int drawResultsTable(const char* id, const std::vector<LeakRow>& rows, const TableOptions& opt, float height) {
  auto& revealed = revealedRows();
  int unlockClicked = -1;
  const int columnCount = 4 + (opt.showUrl ? 1 : 0) + (opt.infoLabel ? 1 : 0);
  if (height == 0.0f) height = tableFitHeight(rows.size());
  tableBegin();
  if (!ImGui::BeginTable(id, columnCount, tableFlags(), ImVec2(0, height))) {
    tableEnd(false);
    return -1;
  }

  ImGui::TableSetupScrollFreeze(0, 1);
  if (opt.showUrl) ImGui::TableSetupColumn("URL", ImGuiTableColumnFlags_WidthStretch, 2.6f);
  ImGui::TableSetupColumn("TYPE", ImGuiTableColumnFlags_WidthStretch, 0.75f);
  ImGui::TableSetupColumn("EMAIL/USERNAME", ImGuiTableColumnFlags_WidthStretch, 2.4f);
  ImGui::TableSetupColumn("PASSWORD", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("INDEXED AT", ImGuiTableColumnFlags_WidthStretch, 1.1f);
  if (opt.infoLabel) ImGui::TableSetupColumn(opt.infoLabel, ImGuiTableColumnFlags_WidthStretch, 1.3f);

  // Header: small, muted, uppercase
  ImGui::PushFont(fonts().caption);
  ImGui::PushStyleColor(ImGuiCol_Text, TextMuted);
  ImGui::TableHeadersRow();
  ImGui::PopStyleColor();
  ImGui::PopFont();

  const float rowHeight = tableRowHeight();
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(rows.size()), rowHeight);
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const LeakRow& r = rows[static_cast<size_t>(i)];
      ImGui::PushID(i);
      ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

      // URL
      if (opt.showUrl) {
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (r.url.empty()) {
          w::textMuted("-");
        } else {
          w::textColored(TextMuted, ICON_LINK);
          ImGui::SameLine(0, 6);
          w::textColored(Link, r.url.c_str());
          if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), b, withAlpha(Link, 160));
            ImGui::SetTooltip("%s\nClick to open, right-click for more", r.url.c_str());
          }
          if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) openUrl(r.url);
          rowContextMenu("ctx_url", r);
        }
      }

      // TYPE
      ImGui::TableNextColumn();
      const bool isEmail = r.isEmail.value_or(looksLikeEmail(r.username));
      w::badge(isEmail ? "Email" : "Username", isEmail ? Green : Blue);

      // EMAIL / USERNAME
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::PushFont(fonts().mono);
      ImGui::TextUnformatted(orDash(r.username));
      ImGui::PopFont();
      rowContextMenu("ctx_user", r);

      // PASSWORD:  [eye] ●●●●●●●●   /   [crossed eye] hunter2   /   (spinner) Unlocking...
      ImGui::TableNextColumn();
      {
        const std::string key = leakKey(r);
        const double now = ImGui::GetTime();
        const bool locked = r.password.empty();
        const bool unlockable = locked && !r.id.empty() && i < opt.unlockLimit;
        const bool unlocking = locked && opt.unlocking && !r.id.empty() && opt.unlocking->count(r.id) > 0;
        auto it = revealed.find(key);
        const bool opened = it != revealed.end();
        const bool pending = unlocking || (opened && !locked && now < it->second);
        const bool shown = opened && !pending && !locked;

        ImGui::AlignTextToFramePadding();
        if (locked && !unlockable && !unlocking) {
          ImGui::PushFont(fonts().mono);
          w::textColored(withAlpha(TextMuted, 110), kDots);
          ImGui::PopFont();
          if (ImGui::IsItemHovered() && opt.unlockLimit > 0 && !r.id.empty())
            ImGui::SetTooltip("Only the first %d rows can be unlocked.", opt.unlockLimit);
        } else if (pending) {
          // Text with room on the left, spinner drawn into that room on the same line.
          const float rad = ImGui::GetFontSize() * 0.38f;
          ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rad * 2.0f + 10.0f);
          w::textColored(TextMuted, "Unlocking...");
          const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
          const ImVec2 c(a.x - 10.0f - rad, (a.y + b.y) * 0.5f);
          ImDrawList* dl = ImGui::GetWindowDrawList();
          const float t = static_cast<float>(now) * 6.0f;
          dl->PathArcTo(c, rad - 1.0f, t, t + 4.2f, 20);
          dl->PathStroke(Accent, 0, 2.2f);
        } else {
          ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 2));
          ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(Accent, shown ? 70 : 0));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, withAlpha(Accent, 110));
          ImGui::PushStyleColor(ImGuiCol_Text, shown ? Text : TextMuted);
          if (ImGui::SmallButton(shown ? ICON_HIDE "##eye" : ICON_EYE "##eye")) {
            if (shown) revealed.erase(key);                      // hide
            else if (!locked) revealed[key] = now + opt.rowDelay;  // show (password already here)
            else unlockClicked = i;                                // fetch it first
          }
          ImGui::PopStyleColor(3);
          ImGui::PopStyleVar();
          if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip(shown ? "Hide password" : "Show password");
          }
          ImGui::SameLine(0, 8);
          ImGui::PushFont(fonts().mono);
          if (shown) ImGui::TextUnformatted(r.password.c_str());
          else w::textColored(TextMuted, kDots);
          ImGui::PopFont();
          if (!locked) rowContextMenu("ctx_pass", r);
        }
      }

      // INDEXED AT
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      w::textColored(TextMuted, ICON_CALENDAR);
      ImGui::SameLine(0, 6);
      ImGui::PushFont(fonts().mono);
      w::textColored(TextMuted, r.addedAt.empty() ? "-" : shortDate(r.addedAt).c_str());
      ImGui::PopFont();

      if (opt.infoLabel) {
        ImGui::TableNextColumn();
        if (r.info == "Employee") w::badge("Employee", Green);
        else if (r.info == "Third party") w::badge("Third party", Blue);
        else if (r.info == "Customer") w::badge("Customer", Orange);
        else w::textMuted(orDash(r.info));
      }
      ImGui::PopID();
    }
  }
  ImGui::EndTable();
  tableEnd(true);
  return unlockClicked;

}

int tableFlags() {
  // No Resizable: it would force vertical column lines.
  return ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
         ImGuiTableFlags_PadOuterX;
}

float tableRowHeight() {
  // Rows hold frame-height widgets (badges, buttons); ImGui adds the cell padding above and below.
  return ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2.0f;
}

float tableFitHeight(size_t rows) {
  const float header = fonts().caption->FontSize + ImGui::GetStyle().CellPadding.y * 2.0f + 2.0f;
  const float wanted = header + static_cast<float>(rows) * (tableRowHeight() + 3.0f) + 10.0f;
  return wanted < ImGui::GetContentRegionAvail().y ? wanted : 0.0f;
}

void tableBegin() {
  ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, Border);
  ImGui::PushStyleColor(ImGuiCol_TableBorderLight, withAlpha(Border, 190));
  ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, withAlpha(Panel, 0));
}

void tableEnd(bool tableDrawn) {
  ImGui::PopStyleColor(3);
  if (tableDrawn) {
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(a, b, Border, 10.0f, 0, 1.0f);
  }
}

bool drawPager(const char* id, int& page, int64_t total, int pageSize, bool disabled) {
  int pages = static_cast<int>(std::max<int64_t>(1, (total + pageSize - 1) / pageSize));
  bool changed = false;
  ImGui::PushID(id);
  const float s = ImGui::GetFrameHeight();
  ImGui::BeginDisabled(disabled || page <= 1);
  if (w::button("\xE2\x80\xB9", w::ButtonKind::Outline, ImVec2(s, s), !(disabled || page <= 1))) { --page; changed = true; }
  ImGui::EndDisabled();
  ImGui::SameLine(0, 8);
  ImGui::AlignTextToFramePadding();
  ImGui::PushFont(fonts().mono);
  ImGui::TextDisabled("%d / %d", page, pages);
  ImGui::PopFont();
  ImGui::SameLine(0, 8);
  ImGui::BeginDisabled(disabled || page >= pages);
  if (w::button("\xE2\x80\xBA", w::ButtonKind::Outline, ImVec2(s, s), !(disabled || page >= pages))) { ++page; changed = true; }
  ImGui::EndDisabled();
  ImGui::PopID();
  return changed;
}

void helpMarker(const char* text) {
  ImGui::TextDisabled("(?)");
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void errorText(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(252, 129, 129, 255));
  ImGui::TextWrapped("%s %s", ICON_WARNING, text.c_str());
  ImGui::PopStyleColor();
}

void mutedText(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, TextMuted);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

void spinner(const char* label) {
  ImGui::AlignTextToFramePadding();
  w::spinner(ImGui::GetFontSize() * 0.5f, Accent);
  ImGui::SameLine(0, 8);
  ImGui::TextDisabled("%s", label);
}

}  // namespace su::ui
