#include "ui/Widgets.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>

#include "ui/Theme.h"

namespace su::ui::w {

using namespace color;

namespace {
ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }

ImU32 dim(ImU32 c, bool enabled) { return enabled ? c : withAlpha(c, 110); }
}  // namespace

bool button(const char* label, ButtonKind kind, ImVec2 size, bool enabled) {
  ImGui::PushFont(fonts().semibold);
  const char* labelEnd = ImGui::FindRenderedTextEnd(label);
  ImVec2 textSize = ImGui::CalcTextSize(label, labelEnd);
  const float padX = ImGui::GetFontSize() * 1.1f, padY = ImGui::GetFontSize() * 0.6f;
  if (size.x <= 0) size.x = textSize.x + padX * 2;
  if (size.y <= 0) size.y = textSize.y + padY * 2;

  ImGui::PushID(label);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::BeginDisabled(!enabled);
  bool clicked = ImGui::InvisibleButton("##btn", size);
  ImGui::EndDisabled();
  bool hovered = enabled && ImGui::IsItemHovered();
  bool active = enabled && ImGui::IsItemActive();
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float rounding = 8.0f;
  ImU32 fill = 0, border = 0, text = Text;
  switch (kind) {
    case ButtonKind::Primary:
      fill = active ? AccentActive : hovered ? AccentHover : Accent;
      break;
    case ButtonKind::Success:
      fill = withAlpha(Green, active ? 200 : hovered ? 255 : 225);
      text = IM_COL32(8, 30, 16, 255);
      break;
    case ButtonKind::Danger:
      fill = withAlpha(Red, active ? 230 : hovered ? 255 : 210);
      break;
    case ButtonKind::Outline:
      fill = active ? PanelActive : hovered ? PanelHover : Panel;
      border = hovered ? withAlpha(Text, 90) : Border;
      break;
    case ButtonKind::Ghost:
      fill = active ? PanelActive : hovered ? PanelHover : 0;
      text = hovered ? Text : TextMuted;
      break;
  }
  if (fill) dl->AddRectFilled(pos, pos + size, dim(fill, enabled), rounding);
  if (border) dl->AddRect(pos, pos + size, dim(border, enabled), rounding, 0, 1.0f);
  ImVec2 tp(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f);
  dl->AddText(tp, dim(text, enabled), label, labelEnd);
  ImGui::PopID();
  ImGui::PopFont();
  return clicked && enabled;
}

float contentMaxX() {
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  return win->ContentRegionRect.Max.x - win->Pos.x;
}

float buttonWidth(const char* label) {
  ImGui::PushFont(fonts().semibold);
  float wdt = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + ImGui::GetFontSize() * 2.2f;
  ImGui::PopFont();
  return wdt;
}

float buttonHeight() {
  ImGui::PushFont(fonts().semibold);
  const float h = ImGui::GetFontSize() * 2.2f;  // text + padY * 2, as in button()
  ImGui::PopFont();
  return h;
}

void roundedGradient(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImU32 left, ImU32 right, float gx0, float gx1) {
  const int v0 = dl->VtxBuffer.Size;
  dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding);
  const int v1 = dl->VtxBuffer.Size;
  const ImVec4 cl = ImGui::ColorConvertU32ToFloat4(left), cr = ImGui::ColorConvertU32ToFloat4(right);
  const float span = std::max(1.0f, gx1 - gx0);
  for (int i = v0; i < v1; ++i) {
    ImDrawVert& v = dl->VtxBuffer[i];
    const float t = std::clamp((v.pos.x - gx0) / span, 0.0f, 1.0f);
    // Keep the anti-aliasing fringe: scale by the alpha the rect itself gave this vertex.
    const float fringe = static_cast<float>((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    v.col = ImGui::ColorConvertFloat4ToU32(ImVec4(cl.x + (cr.x - cl.x) * t, cl.y + (cr.y - cl.y) * t,
                                                  cl.z + (cr.z - cl.z) * t, (cl.w + (cr.w - cl.w) * t) * fringe));
  }
}

float pillWidth(const char* text) {
  ImGui::PushFont(fonts().semibold);
  float wdt = ImGui::CalcTextSize(text).x + ImGui::GetFontSize() * 1.6f;
  ImGui::PopFont();
  return wdt;
}

bool pill(const char* id, const char* text, ImU32 col, const char* tooltip) {
  ImGui::PushFont(fonts().semibold);
  ImVec2 ts = ImGui::CalcTextSize(text);
  ImVec2 size(ts.x + ImGui::GetFontSize() * 1.6f, ts.y + ImGui::GetFontSize() * 0.75f);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float r = size.y * 0.5f;
  dl->AddRectFilled(pos, pos + size, withAlpha(col, hovered ? 50 : 30), r);
  dl->AddRect(pos, pos + size, withAlpha(col, 150), r, 0, 1.0f);
  dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f), col, text);
  ImGui::PopFont();
  if (tooltip && hovered) ImGui::SetTooltip("%s", tooltip);
  return clicked;
}

void badge(const char* text, ImU32 col) {
  ImGui::PushFont(fonts().caption);
  ImVec2 ts = ImGui::CalcTextSize(text);
  ImVec2 size(ts.x + 14.0f, ts.y + 7.0f);
  // Vertically centred on a frame-height line so it aligns with text next to it.
  const float lineH = std::max(size.y, ImGui::GetFrameHeight());
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(size.x, lineH));
  pos.y += (lineH - size.y) * 0.5f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(pos, pos + size, withAlpha(col, 28), 5.0f);
  dl->AddRect(pos, pos + size, withAlpha(col, 170), 5.0f, 0, 1.0f);
  dl->AddText(ImVec2(pos.x + 7.0f, pos.y + 3.5f), col, text);
  ImGui::PopFont();
}

bool categoryCard(const char* id, const char* icon, const char* label, const std::string& count, ImU32 col,
                  bool selected, float width) {
  const float h = ImGui::GetFontSize() * 2.6f;
  ImVec2 pos = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton(id, ImVec2(width, h));
  bool hovered = ImGui::IsItemHovered();
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 end(pos.x + width, pos.y + h);
  dl->AddRectFilled(pos, end, selected ? withAlpha(col, 30) : hovered ? PanelHover : Panel, 10.0f);
  dl->AddRect(pos, end, selected ? withAlpha(col, 200) : Border, 10.0f, 0, selected ? 1.5f : 1.0f);

  const float pad = ImGui::GetFontSize() * 0.9f;
  const float ty = pos.y + (h - ImGui::GetFontSize()) * 0.5f;
  dl->AddText(ImVec2(pos.x + pad, ty), col, icon);
  dl->AddText(ImVec2(pos.x + pad + ImGui::GetFontSize() * 1.5f, ty), selected ? Text : TextMuted, label);
  ImGui::PushFont(fonts().semibold);
  ImVec2 cs = ImGui::CalcTextSize(count.c_str());
  dl->AddText(ImVec2(end.x - pad - cs.x, pos.y + (h - cs.y) * 0.5f), Text, count.c_str());
  ImGui::PopFont();
  return clicked;
}

bool searchBar(const char* id, const char* hint, std::string& value, float width, float height) {
  ImGui::PushID(id);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 end(pos.x + width, pos.y + height);
  dl->AddRectFilled(pos, end, InputBg, 10.0f);

  const float iconX = ImGui::GetFontSize() * 1.0f;
  const float textX = ImGui::GetFontSize() * 2.6f;
  dl->AddText(ImVec2(pos.x + iconX, pos.y + (height - ImGui::GetFontSize()) * 0.5f - 1.0f), TextMuted, ICON_SEARCH);

  ImGui::PushFont(fonts().mono);
  const float fh = ImGui::GetFontSize();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, (height - fh) * 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
  ImGui::SetCursorScreenPos(ImVec2(pos.x + textX, pos.y));
  ImGui::SetNextItemWidth(width - textX - ImGui::GetFontSize());
  bool enter = ImGui::InputTextWithHint("##input", hint, &value, ImGuiInputTextFlags_EnterReturnsTrue);
  bool focused = ImGui::IsItemActive();
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar(2);
  ImGui::PopFont();

  dl->AddRect(pos, end, focused ? withAlpha(Accent, 220) : withAlpha(Border, 160), 10.0f, 0, focused ? 1.5f : 1.0f);
  ImGui::SetCursorScreenPos(pos);
  ImGui::Dummy(ImVec2(width, height));
  ImGui::PopID();
  return enter;
}

bool tabStrip(const char* id, const char* const* labels, int count, int& current) {
  bool changed = false;
  ImGui::PushID(id);
  ImGui::PushFont(fonts().semibold);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (int i = 0; i < count; ++i) {
    if (i) ImGui::SameLine(0, 4.0f);
    ImVec2 ts = ImGui::CalcTextSize(labels[i]);
    ImVec2 size(ts.x + ImGui::GetFontSize() * 1.4f, ts.y + ImGui::GetFontSize() * 0.9f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(i);
    if (ImGui::InvisibleButton("##tab", size) && current != i) {
      current = i;
      changed = true;
    }
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    bool sel = current == i;
    if (sel) dl->AddRectFilled(pos, pos + size, PanelActive, 8.0f);
    else if (hovered) dl->AddRectFilled(pos, pos + size, Panel, 8.0f);
    dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f), sel ? Text : TextMuted,
                labels[i]);
  }
  ImGui::PopFont();
  ImGui::PopID();
  return changed;
}

bool navLink(const char* label, bool active) {
  ImGui::PushFont(fonts().semibold);
  ImVec2 ts = ImGui::CalcTextSize(label);
  ImVec2 size(ts.x + 20.0f, ts.y + 12.0f);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton(label, size);
  bool hovered = ImGui::IsItemHovered();
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (active || hovered) dl->AddRectFilled(pos, pos + size, active ? PanelActive : Panel, 8.0f);
  dl->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f), active ? Text : TextMuted, label);
  ImGui::PopFont();
  return clicked;
}

bool iconButton(const char* id, const char* icon, const char* tooltip, int64_t counter, float size) {
  if (size <= 0) size = ImGui::GetFontSize() * 2.0f;
  ImVec2 pos = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
  bool hovered = ImGui::IsItemHovered();
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (hovered) dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), Panel, 8.0f);
  ImVec2 is = ImGui::CalcTextSize(icon);
  dl->AddText(ImVec2(pos.x + (size - is.x) * 0.5f, pos.y + (size - is.y) * 0.5f - 1.0f), hovered ? Text : TextMuted,
              icon);
  if (counter > 0) {
    char buf[16];
    snprintf(buf, sizeof buf, counter > 99 ? "99+" : "%lld", static_cast<long long>(counter));
    ImGui::PushFont(fonts().caption);
    ImVec2 cs = ImGui::CalcTextSize(buf);
    float r = std::max(cs.y * 0.5f + 1.0f, cs.x * 0.5f + 4.0f);
    ImVec2 c(pos.x + size - r * 0.6f, pos.y + r * 0.6f);
    dl->AddCircleFilled(c, r, Red);
    dl->AddText(ImVec2(c.x - cs.x * 0.5f, c.y - cs.y * 0.5f), IM_COL32_WHITE, buf);
    ImGui::PopFont();
  }
  if (tooltip && hovered) ImGui::SetTooltip("%s", tooltip);
  return clicked;
}

bool avatar(const char* id, const char* initial, float size) {
  ImVec2 pos = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton(id, ImVec2(size, size));
  bool hovered = ImGui::IsItemHovered();
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 c(pos.x + size * 0.5f, pos.y + size * 0.5f);
  dl->AddCircleFilled(c, size * 0.5f, hovered ? PanelActive : IM_COL32(72, 76, 108, 255));
  ImGui::PushFont(fonts().semibold);
  ImVec2 ts = ImGui::CalcTextSize(initial);
  dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), Text, initial);
  ImGui::PopFont();
  return clicked;
}

void statBlock(const char* value, const char* caption) {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
  ImGui::BeginGroup();
  ImGui::PushFont(fonts().monoMedium);
  ImVec2 vs = ImGui::CalcTextSize(value);
  ImGui::PopFont();
  ImGui::PushFont(fonts().caption);
  ImVec2 cs = ImGui::CalcTextSize(caption);
  ImGui::PopFont();
  float w = std::max(vs.x, cs.x);
  float x = ImGui::GetCursorPosX();
  ImGui::PushFont(fonts().monoMedium);
  ImGui::SetCursorPosX(x + w - vs.x);
  ImGui::TextUnformatted(value);
  ImGui::PopFont();
  ImGui::PushFont(fonts().caption);
  ImGui::SetCursorPosX(x + w - cs.x);
  ImGui::PushStyleColor(ImGuiCol_Text, TextMuted);
  ImGui::TextUnformatted(caption);
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::EndGroup();
  ImGui::PopStyleVar();
}

void textColored(ImU32 col, const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, col);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}

void textMuted(const char* text) { textColored(TextMuted, text); }

void heading(const char* text) {
  ImGui::PushFont(fonts().title);
  ImGui::TextUnformatted(text);
  ImGui::PopFont();
}

void spinner(float radius, ImU32 col) {
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(radius * 2, radius * 2));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float t = static_cast<float>(ImGui::GetTime()) * 6.0f;
  ImVec2 c(pos.x + radius, pos.y + radius);
  dl->PathClear();
  const int n = 24;
  for (int i = 0; i <= n; ++i) {
    float a = t + (static_cast<float>(i) / n) * 4.2f;
    dl->PathLineTo(ImVec2(c.x + std::cos(a) * (radius - 2), c.y + std::sin(a) * (radius - 2)));
  }
  dl->PathStroke(col, 0, 2.5f);
}

void hyperlinkCursor() {
  if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

}  // namespace su::ui::w
