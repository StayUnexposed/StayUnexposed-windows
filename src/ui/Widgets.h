#pragma once
#include <cstdint>
#include <string>

#include <imgui.h>

namespace su::ui::w {

enum class ButtonKind { Primary, Outline, Ghost, Danger, Success };

// Rounded button. `label` may start with an icon glyph. size.x/y 0 = fit content.
bool button(const char* label, ButtonKind kind = ButtonKind::Primary, ImVec2 size = ImVec2(0, 0), bool enabled = true);

// Right edge of the current window's content region, in window-local X (for right alignment).
float contentMaxX();

// Width a button()/pill() with this label will take (for right alignment).
float buttonWidth(const char* label);
float buttonHeight();  // height of a button() with default size (for vertical centring)

// Horizontal gradient filling a rounded rectangle (corners stay clipped, unlike AddRectFilledMultiColor).
// The colour goes from `left` at x = gx0 to `right` at x = gx1 (clamped outside that range).
void roundedGradient(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImU32 left, ImU32 right, float gx0, float gx1);
float pillWidth(const char* text);

// Small rounded capsule with tinted background, e.g. "★ 2,353 points". Returns true when clicked.
bool pill(const char* id, const char* text, ImU32 color, const char* tooltip = nullptr);

// Outlined label, e.g. the Email / Username type badge.
void badge(const char* text, ImU32 color);

// Large selectable card showing an icon, a label and a count (domain categories).
bool categoryCard(const char* id, const char* icon, const char* label, const std::string& count, ImU32 color,
                  bool selected, float width);

// Dark rounded search field with a magnifier icon. Returns true when Enter is pressed.
bool searchBar(const char* id, const char* hint, std::string& value, float width, float height);

// Row of pill-shaped tabs. Returns true when the selection changed.
bool tabStrip(const char* id, const char* const* labels, int count, int& current);

// Text link in the header navigation.
bool navLink(const char* label, bool active);

// Square icon button with optional red counter.
bool iconButton(const char* id, const char* icon, const char* tooltip, int64_t counter = 0, float size = 0);

// Circle with the user's initial.
bool avatar(const char* id, const char* initial, float size);

// Vertical "value / caption" block (stats banner).
void statBlock(const char* value, const char* caption);

// Text helpers
void textColored(ImU32 color, const char* text);
void textMuted(const char* text);
void heading(const char* text);
void spinner(float radius, ImU32 color);
void hyperlinkCursor();

}  // namespace su::ui::w
