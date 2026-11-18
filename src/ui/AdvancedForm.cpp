#include "ui/AdvancedForm.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/Util.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace su::ui {

using namespace color;

namespace {

const char* kMatchLabels[] = {"Contains", "Starts with", "Ends with"};
constexpr const char* kArrow = " \xE2\x86\x92 ";  // " -> "

ImVec2 textSize(ImFont* f, const char* s) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0, s); }

// Small "(i)" with a tooltip.
void infoIcon(const char* help) {
  ImGui::SameLine(0, 6);
  ImGui::PushFont(fonts().caption);
  w::textColored(TextMuted, ICON_INFO);
  ImGui::PopFont();
  if (ImGui::IsItemHovered() && help) ImGui::SetTooltip("%s", help);
}

// One line mixing normal and bold pieces: {{"Between fields", true}, {" -> AND", false}}.
struct Piece {
  const char* text;
  bool bold;
};
void richLine(std::initializer_list<Piece> pieces, ImU32 col = Text) {
  bool first = true;
  for (const Piece& p : pieces) {
    if (!first) ImGui::SameLine(0, 0);
    first = false;
    if (p.bold) ImGui::PushFont(fonts().semibold);
    w::textColored(p.bold ? Text : col, p.text);
    if (p.bold) ImGui::PopFont();
  }
}

void bullet() {
  w::textColored(TextMuted, "\xE2\x80\xA2");  // U+2022
  ImGui::SameLine(0, 8);
}

// Key cap, e.g. [Enter].
void keyCap(const char* key) {
  ImFont* f = fonts().caption;
  const ImVec2 ts = textSize(f, key);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = ImGui::GetTextLineHeight();
  const ImVec2 size(ts.x + 12.0f, h);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), PanelActive, 6.0f);
  dl->AddText(f, f->FontSize, ImVec2(p.x + 6.0f, p.y + (h - ts.y) * 0.5f), Text, key);
  ImGui::Dummy(size);
}

void sectionHeader(const char* icon, const char* title) {
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  ImGui::PushFont(fonts().caption);
  w::textColored(TextMuted, icon);
  ImGui::SameLine(0, 8);
  w::textColored(Text, title);
  ImGui::PopFont();
}

// Rounded on/off switch. Returns true when toggled.
bool toggleSwitch(const char* id, bool& value) {
  const float h = ImGui::GetFontSize() * 1.1f, wdt = h * 1.8f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton(id, ImVec2(wdt, h));
  if (clicked) value = !value;
  if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + wdt, p.y + h), value ? Accent : PanelActive, h * 0.5f);
  const float r = h * 0.5f - 2.0f;
  const float cx = value ? p.x + wdt - h * 0.5f : p.x + h * 0.5f;
  dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, IM_COL32_WHITE, 20);
  return clicked;
}

// Text that may hold several values: split on commas, semicolons and line breaks.
std::vector<std::string> splitValues(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ',' || c == ';' || c == '\n' || c == '\r') {
      out.push_back(trim(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.push_back(trim(cur));
  return out;
}

}  // namespace

void AdvancedForm::clear() {
  q_ = AdvQuery{};
  for (auto& row : buf_) row[0].clear(), row[1].clear();
  for (auto& row : err_) row[0].clear(), row[1].clear();
}

std::string AdvancedForm::summary() const {
  const int n = q_.valueCount();
  if (n == 0) return "No filter yet";
  std::string names;
  int fields = 0;
  for (int i = 0; i < kFields; ++i) {
    const AdvFilter& f = q_.fields[i];
    if (f.include.empty() && f.exclude.empty()) continue;
    ++fields;
    if (fields <= 4) names += (names.empty() ? "" : ", ") + std::string(advFieldInfo(static_cast<AdvField>(i)).label);
  }
  if (fields > 4) names += ", +" + std::to_string(fields - 4) + " more";
  return std::to_string(n) + (n == 1 ? " value in " : " values in ") + std::to_string(fields) +
         (fields == 1 ? " field: " : " fields: ") + names;
}

// Moves typed text into chips. `all` = also the part after the last separator (Enter, Tab, focus lost).
void AdvancedForm::commit(AdvField f, bool exclude, bool all) {
  const int i = static_cast<int>(f), e = exclude ? 1 : 0;
  std::string& buf = buf_[i][e];
  auto parts = splitValues(buf);
  std::string keep = all ? std::string() : parts.back();
  if (!all) parts.pop_back();
  auto& values = exclude ? q_.fields[i].exclude : q_.fields[i].include;
  err_[i][e].clear();
  std::string rejected;
  for (const std::string& v : parts) {
    if (v.empty()) continue;
    if (std::find(values.begin(), values.end(), v) != values.end()) continue;  // already there
    if (static_cast<int>(values.size()) >= kAdvMaxValuesPerField) {
      err_[i][e] = "At most " + std::to_string(kAdvMaxValuesPerField) + " values per field.";
      break;
    }
    const std::string why = advValueError(f, v);
    if (!why.empty()) {
      err_[i][e] = "\"" + v + "\": " + why;
      rejected += (rejected.empty() ? "" : ", ") + v;
      continue;
    }
    values.push_back(v);
  }
  // Rejected values stay in the box so they can be corrected.
  buf = rejected.empty() ? keep : (keep.empty() ? rejected : rejected + ", " + keep);
}

bool AdvancedForm::tagBox(AdvField f, bool exclude, float width) {
  const AdvFieldInfo& info = advFieldInfo(f);
  const int i = static_cast<int>(f), e = exclude ? 1 : 0;
  AdvFilter& filter = q_.fields[i];
  auto& values = exclude ? filter.exclude : filter.include;
  MatchType& match = exclude ? filter.excludeMatch : filter.includeMatch;
  std::string& buf = buf_[i][e];
  const std::string& err = err_[i][e];
  ImGui::PushID(i * 2 + e);

  const float fs = ImGui::GetFontSize();
  const float pad = fs * 0.45f, lineH = fs * 1.9f, gap = 6.0f;
  ImFont* cap = fonts().caption;
  const float chipH = fs * 1.45f;
  const float inner = width - pad * 2;

  // ---- layout: [match] [chip] [chip] ... [input], wrapping onto more lines when needed
  struct Slot { float x, w; int line; };
  int line = 0;
  float cx = 0.0f;
  auto place = [&](float w) {
    if (cx > 0.0f && cx + w > inner) {
      ++line;
      cx = 0.0f;
    }
    Slot s{cx, w, line};
    cx += w + gap;
    return s;
  };
  const char* matchLabel = kMatchLabels[static_cast<int>(match)];
  Slot matchSlot{};
  if (info.matchType) matchSlot = place(textSize(cap, matchLabel).x + fs * 1.5f);
  std::vector<Slot> chips;
  for (const auto& v : values) chips.push_back(place(std::min(textSize(fonts().regular, v.c_str()).x + fs * 1.8f, inner)));
  const float minInput = fs * 7.0f;
  if (cx > 0.0f && cx + minInput > inner) {
    ++line;
    cx = 0.0f;
  }
  const Slot inputSlot{cx, inner - cx, line};
  const float boxH = pad * 2 + lineH * (line + 1) - (lineH - fs * 1.9f);

  // ---- frame
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 end(p.x + width, p.y + boxH);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 fill = exclude ? IM_COL32(58, 50, 52, 255) : IM_COL32(36, 38, 56, 255);
  ImU32 edge = exclude ? withAlpha(Orange, 120) : Border;
  if (!err.empty()) edge = withAlpha(Red, 200);
  dl->AddRectFilled(p, end, fill, 8.0f);
  dl->AddRect(p, end, edge, 8.0f);
  auto slotPos = [&](const Slot& s, float h) {
    return ImVec2(p.x + pad + s.x, p.y + pad + s.line * lineH + (lineH - h) * 0.5f);
  };

  bool changed = false;
  // ---- match type menu
  if (info.matchType) {
    const ImVec2 mp = slotPos(matchSlot, chipH);
    ImGui::SetCursorScreenPos(mp);
    if (ImGui::InvisibleButton("##match", ImVec2(matchSlot.w, chipH))) ImGui::OpenPopup("##match_menu");
    const bool hov = ImGui::IsItemHovered();
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    dl->AddRectFilled(mp, ImVec2(mp.x + matchSlot.w, mp.y + chipH), hov ? PanelHover : PanelActive, 6.0f);
    const ImVec2 ts = textSize(cap, matchLabel);
    dl->AddText(cap, cap->FontSize, ImVec2(mp.x + 8.0f, mp.y + (chipH - ts.y) * 0.5f), Text, matchLabel);
    dl->AddText(cap, cap->FontSize * 0.8f, ImVec2(mp.x + matchSlot.w - fs * 0.85f, mp.y + (chipH - ts.y * 0.8f) * 0.5f + 1),
                TextMuted, ICON_CHEVRON_DOWN);
    if (ImGui::BeginPopup("##match_menu")) {
      for (int m = 0; m < 3; ++m)
        if (ImGui::Selectable(kMatchLabels[m], static_cast<int>(match) == m)) {
          match = static_cast<MatchType>(m);
          changed = true;
        }
      ImGui::EndPopup();
    }
    if (hov) ImGui::SetTooltip("Applies to every value in this field");
  }

  // ---- chips
  int removeAt = -1;
  const ImU32 chipCol = exclude ? Orange : Accent;
  for (size_t k = 0; k < values.size(); ++k) {
    const Slot& s = chips[k];
    const ImVec2 cp = slotPos(s, chipH);
    dl->AddRectFilled(cp, ImVec2(cp.x + s.w, cp.y + chipH), withAlpha(chipCol, 55), 6.0f);
    dl->AddRect(cp, ImVec2(cp.x + s.w, cp.y + chipH), withAlpha(chipCol, 120), 6.0f);
    const float xw = fs * 1.1f;
    dl->PushClipRect(cp, ImVec2(cp.x + s.w - xw, cp.y + chipH), true);
    dl->AddText(ImVec2(cp.x + 8.0f, cp.y + (chipH - fs) * 0.5f), Text, values[k].c_str());
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(cp.x + s.w - xw, cp.y));
    ImGui::PushID(static_cast<int>(k));
    if (ImGui::InvisibleButton("##x", ImVec2(xw, chipH))) removeAt = static_cast<int>(k);
    const bool xh = ImGui::IsItemHovered();
    ImGui::PopID();
    if (xh) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      ImGui::SetTooltip("Remove \"%s\"", values[k].c_str());
    }
    const ImVec2 xs = textSize(cap, ICON_CLOSE);
    dl->AddText(cap, cap->FontSize * 0.8f, ImVec2(cp.x + s.w - xw + (xw - xs.x * 0.8f) * 0.5f - 2, cp.y + (chipH - xs.y * 0.8f) * 0.5f),
                xh ? Text : TextMuted, ICON_CLOSE);
  }
  if (removeAt >= 0) {
    values.erase(values.begin() + removeAt);
    changed = true;
  }

  // ---- text input
  const ImGuiID inputId = ImGui::GetID("##in");
  // A pasted multi-line list: turn line breaks into commas first (single-line inputs drop them).
  if (ImGui::GetActiveID() == inputId && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
    if (const char* clip = ImGui::GetClipboardText()) {
      std::string c = clip;
      if (c.find_first_of("\r\n") != std::string::npos) {
        for (auto& ch : c)
          if (ch == '\r' || ch == '\n') ch = ',';
        ImGui::SetClipboardText(c.c_str());
      }
    }
  }
  const ImVec2 ip = slotPos(inputSlot, ImGui::GetFrameHeight());
  ImGui::SetCursorScreenPos(ip);
  ImGui::SetNextItemWidth(inputSlot.w);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  bool tab = false;
  const bool enter = ImGui::InputTextWithHint(
      "##in", !values.empty() ? "" : exclude ? (info.excludeHint ? info.excludeHint : "") : info.includeHint,
      &buf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion,
      [](ImGuiInputTextCallbackData* d) {
        if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion) *static_cast<bool*>(d->UserData) = true;
        return 0;
      },
      &tab);
  const bool lostFocus = ImGui::IsItemDeactivatedAfterEdit();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
  if (enter || tab || lostFocus) {
    commit(f, exclude, true);
    changed = true;
    if (enter || tab) ImGui::SetKeyboardFocusHere(-1);  // keep typing in the same field
  } else if (buf.find_first_of(",;") != std::string::npos) {
    commit(f, exclude, false);  // typed or pasted separators: everything before them becomes chips
    changed = true;
  }
  // Clicking anywhere on the frame focuses the input.
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(p, end) && !ImGui::IsAnyItemHovered())
    ImGui::SetKeyboardFocusHere(-1);

  ImGui::SetCursorScreenPos(ImVec2(p.x, end.y));
  ImGui::Dummy(ImVec2(width, 0));
  if (!err.empty()) {
    ImGui::PushFont(cap);
    w::textColored(IM_COL32(252, 129, 129, 255), err.c_str());
    ImGui::PopFont();
  }
  ImGui::PopID();
  return changed;
}

bool AdvancedForm::fieldRow(AdvField f, float labelW, float colW) {
  const AdvFieldInfo& info = advFieldInfo(f);
  const float gap = ImGui::GetStyle().ItemSpacing.x * 1.4f;
  const float y0 = ImGui::GetCursorPosY();
  const float x0 = ImGui::GetCursorPosX();
  bool changed = false;

  // label (vertically centred on the first line of the boxes)
  ImGui::SetCursorPos(ImVec2(x0 + 12.0f, y0 + (ImGui::GetFontSize() * 2.8f - ImGui::GetTextLineHeight()) * 0.5f));
  ImGui::PushFont(fonts().semibold);
  ImGui::TextUnformatted(info.label);
  ImGui::PopFont();
  infoIcon(info.help);

  ImGui::SetCursorPos(ImVec2(x0 + labelW, y0));
  ImGui::BeginGroup();
  changed |= tagBox(f, false, colW);
  ImGui::EndGroup();
  const float yInc = ImGui::GetCursorPosY();

  ImGui::SetCursorPos(ImVec2(x0 + labelW + colW + gap, y0));
  float yExc = y0;
  if (info.excludeHint) {
    ImGui::BeginGroup();
    changed |= tagBox(f, true, colW);
    ImGui::EndGroup();
    yExc = ImGui::GetCursorPosY();
  } else {
    ImGui::SetCursorPos(ImVec2(x0 + labelW + colW + gap + 14.0f,
                               y0 + (ImGui::GetFontSize() * 2.8f - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::PushFont(fonts().caption);
    w::textMuted("Include only");
    ImGui::PopFont();
    yExc = y0 + ImGui::GetFontSize() * 2.8f;
  }
  ImGui::SetCursorPos(ImVec2(x0, std::max(yInc, yExc) + 4.0f));
  return changed;
}

void AdvancedForm::drawHelp() {
  const float half = ImGui::GetContentRegionAvail().x * 0.5f;
  const float x0 = ImGui::GetCursorPosX();
  const float y0 = ImGui::GetCursorPosY();

  // Left: how filters work
  ImGui::BeginGroup();
  w::textColored(Green, ICON_CHECK);
  ImGui::SameLine(0, 6);
  ImGui::PushFont(fonts().semibold);
  w::textColored(TextMuted, "How filters work");
  ImGui::PopFont();
  bullet();
  richLine({{"Between fields", true}, {kArrow, false}, {"AND (must match all)", false}});
  bullet();
  richLine({{"Within a field", true}, {kArrow, false}, {"OR (match any value)", false}});
  bullet();
  richLine({{"Exclude fields", true}, {kArrow, false}, {"Remove matching results", false}});
  bullet();
  richLine({{"Characters are searched literally. Choose contains, starts with or ends with.", false}});
  ImGui::EndGroup();
  const float yLeft = ImGui::GetCursorPosY();

  // Right: quick tips
  ImGui::SetCursorPos(ImVec2(x0 + half, y0));
  ImGui::BeginGroup();
  w::textColored(Orange, ICON_LIGHTBULB);
  ImGui::SameLine(0, 6);
  ImGui::PushFont(fonts().semibold);
  w::textColored(TextMuted, "Quick tips");
  ImGui::PopFont();
  bullet();
  ImGui::TextUnformatted("Press");
  ImGui::SameLine(0, 6);
  keyCap("Enter");
  ImGui::SameLine(0, 4);
  keyCap("Tab");
  ImGui::SameLine(0, 6);
  ImGui::TextUnformatted("or");
  ImGui::SameLine(0, 6);
  keyCap(",");
  ImGui::SameLine(0, 6);
  ImGui::TextUnformatted("to add values");
  bullet();
  ImGui::TextUnformatted("Paste comma or newline-separated lists");
  bullet();
  richLine({{"Match type", true}, {kArrow, false}, {"Applies to every value in the field", false}});
  bullet();
  ImGui::TextUnformatted("Hover");
  ImGui::SameLine(0, 5);
  ImGui::PushFont(fonts().caption);
  w::textColored(TextMuted, ICON_INFO);
  ImGui::PopFont();
  ImGui::SameLine(0, 5);
  ImGui::TextUnformatted("icons for field explanations");
  ImGui::EndGroup();

  ImGui::SetCursorPos(ImVec2(x0, std::max(yLeft, ImGui::GetCursorPosY())));
  ImGui::Spacing();
  // View examples
  ImGui::PushStyleColor(ImGuiCol_Text, Link);
  const char* toggle = examples_ ? ICON_CHEVRON_UP "  Hide examples" : ICON_CHEVRON_DOWN "  View examples";
  ImGui::PushFont(fonts().caption);
  ImGui::TextUnformatted(toggle);
  ImGui::PopFont();
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (ImGui::IsItemClicked()) examples_ = !examples_;
  if (examples_) {
    struct Ex { const char* title; const char* how; };
    const Ex ex[] = {
        {"Admin panels of one company", "URL contains /admin  +  Email domain company.com"},
        {"Company accounts used on GitHub", "URL domain github.com  +  Email domain company.com"},
        {"Leave out test accounts", "Username / email exclude test, sample, noreply"},
        {"Recent weak passwords", "Password strength Weak  +  Indexed from 2026-01-01"},
    };
    for (const Ex& e : ex) {
      bullet();
      richLine({{e.title, true}, {kArrow, false}, {e.how, false}}, TextMuted);
    }
  }
}

bool AdvancedForm::draw() {
  const float fs = ImGui::GetFontSize();
  bool search = false;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, Panel);
  ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(Accent, 90));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.4f, fs * 1.0f));
  ImGui::BeginChild("adv_form", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                                                 ImGuiChildFlags_AlwaysUseWindowPadding);

  // Title line
  {
    const char* sub = "Combine simple filters (include/exclude) to find exactly the leaked credentials you need.";
    const float tw = ImGui::CalcTextSize(sub).x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowWidth() - tw) * 0.5f));
    w::textMuted(sub);
    ImGui::Spacing();
  }
  drawHelp();

  // Column headers
  const float avail = ImGui::GetContentRegionAvail().x;
  const float labelW = std::max(fs * 13.5f, avail * 0.17f);
  const float gap = ImGui::GetStyle().ItemSpacing.x * 1.4f;
  const float colW = (avail - labelW - gap) * 0.5f;
  ImGui::Spacing();
  ImGui::Separator();
  {
    const float y = ImGui::GetCursorPosY();
    const float x0 = ImGui::GetCursorPosX();
    ImGui::SetCursorPos(ImVec2(x0 + labelW, y + 4));
    ImGui::PushFont(fonts().caption);
    w::textColored(Green, ICON_CHECK);
    ImGui::SameLine(0, 6);
    w::textColored(Text, "INCLUDE");
    ImGui::SameLine(0, 6);
    w::textMuted("must match");
    ImGui::SetCursorPos(ImVec2(x0 + labelW + colW + gap, y + 4));
    w::textColored(Orange, ICON_CLOSE);
    ImGui::SameLine(0, 6);
    w::textColored(Orange, "EXCLUDE");
    ImGui::SameLine(0, 6);
    w::textMuted("Remove matching results");
    ImGui::PopFont();
  }

  // Field sections
  sectionHeader(ICON_LINK, "URL & DOMAIN");
  for (AdvField f : {AdvField::Url, AdvField::UrlHost, AdvField::UrlDomain, AdvField::UrlScheme, AdvField::UrlTld,
                     AdvField::UrlPort})
    fieldRow(f, labelW, colW);
  sectionHeader(ICON_PERSON, "CREDENTIALS");
  for (AdvField f : {AdvField::Username, AdvField::Password, AdvField::UsernameHash, AdvField::PasswordHash})
    fieldRow(f, labelW, colW);
  sectionHeader(ICON_MAIL, "EMAIL BREAKDOWN");
  for (AdvField f : {AdvField::EmailHost, AdvField::EmailDomain, AdvField::EmailTld}) fieldRow(f, labelW, colW);

  // More filters: four columns
  sectionHeader(ICON_FILTER, "MORE FILTERS");
  {
    const float cw = (ImGui::GetContentRegionAvail().x - gap * 3) / 4.0f;
    const float x0 = ImGui::GetCursorPosX(), y0 = ImGui::GetCursorPosY();
    auto column = [&](int c, const char* label, const char* help) {
      ImGui::SetCursorPos(ImVec2(x0 + c * (cw + gap), y0));
      ImGui::PushFont(fonts().semibold);
      ImGui::TextUnformatted(label);
      ImGui::PopFont();
      infoIcon(help);
      ImGui::SetCursorPosX(x0 + c * (cw + gap));
      ImGui::SetNextItemWidth(cw);
    };
    static const char* idKinds[] = {"All", "Emails only", "Usernames only"};
    static const char* strengths[] = {"Any", "Too weak", "Weak", "Medium", "Strong"};
    int idKind = static_cast<int>(q_.idKind);
    column(0, "Identifier type", "Logins that are email addresses, usernames, or both.");
    if (ImGui::Combo("##idkind", &idKind, idKinds, IM_ARRAYSIZE(idKinds))) q_.idKind = static_cast<IdKind>(idKind);
    column(1, "Password strength", "Too weak = score 0-2, weak = 3-4, medium = 5-7, strong = 8 and more.");
    ImGui::Combo("##strength", &q_.strength, strengths, IM_ARRAYSIZE(strengths));
    auto dateBox = [&](int c, const char* label, const char* id, std::string& value) {
      column(c, label, c == 2 ? "Only leaks indexed on or after this day (UTC)." : "Only leaks indexed on or before this day (UTC).");
      const bool bad = !trim(value).empty() && !isIsoDate(trim(value));
      if (bad) ImGui::PushStyleColor(ImGuiCol_Border, withAlpha(Red, 220));
      if (bad) ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
      ImGui::InputTextWithHint(id, "YYYY-MM-DD", &value, ImGuiInputTextFlags_CharsNoBlank);
      if (bad) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use the format YYYY-MM-DD, e.g. 2026-01-31");
      }
    };
    dateBox(2, "Indexed from", "##from", q_.addedFrom);
    dateBox(3, "Indexed to", "##to", q_.addedTo);
  }

  ImGui::Spacing();
  ImGui::Spacing();
  toggleSwitch("##and", q_.forceAnd);
  ImGui::SameLine(0, 10);
  ImGui::PushFont(fonts().semibold);
  ImGui::TextUnformatted("Require all values in each field (AND)");
  ImGui::PopFont();
  infoIcon("Off: a field matches when any of its values matches (OR). On: every value must match.");
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 2.6f);
  ImGui::PushFont(fonts().caption);
  w::textMuted("Example: URL contains admin and login will only return URLs containing both.");
  ImGui::PopFont();

  // Footer: summary + Search
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  {
    const std::string sum = summary();
    const bool any = q_.hasFilters();
    const char* label = ICON_SEARCH "  Search";
    const char* clearLabel = "Clear all";
    const float bw = w::buttonWidth(label), cw = w::buttonWidth(clearLabel);
    const float y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(y + (w::buttonHeight() - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::PushFont(fonts().semibold);
    w::textColored(any ? Text : TextMuted, sum.c_str());
    ImGui::PopFont();
    const float right = w::contentMaxX();
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    if (any) {
      ImGui::SetCursorPos(ImVec2(right - bw - sp - cw, y));
      if (w::button(clearLabel, w::ButtonKind::Ghost)) clear();
    }
    ImGui::SetCursorPos(ImVec2(right - bw, y));
    if (w::button(label, w::ButtonKind::Primary, ImVec2(bw, 0), any)) search = true;
    if (!any && ImGui::IsItemHovered()) ImGui::SetTooltip("Add at least one value to a field first.");
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
  return search;
}

void AdvancedForm::loadSampleData() {
  clear();
  q_[AdvField::Url].include = {"/admin", "login.php"};
  q_[AdvField::UrlDomain].include = {"acme-corp.com"};
  q_[AdvField::UrlDomain].includeMatch = MatchType::EndsWith;
  q_[AdvField::Url].exclude = {"staging"};
  q_[AdvField::Username].exclude = {"test", "sample"};
  q_[AdvField::EmailDomain].include = {"acme-corp.com"};
  q_.strength = 2;
  buf_[static_cast<int>(AdvField::UrlPort)][0] = "8080";
}

}  // namespace su::ui
