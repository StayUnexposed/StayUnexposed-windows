#pragma once
#include <imgui.h>

// Colours, fonts and icon glyphs shared by every view.
// Icons come from the Windows icon font (Segoe Fluent Icons / Segoe MDL2 Assets),
// merged into the text fonts so they can be used inline in any label.

#define ICON_SEARCH     ""
#define ICON_LOCK       ""
#define ICON_UNLOCK     ""
#define ICON_DOWNLOAD   ""
#define ICON_BELL       ""
#define ICON_CALENDAR   ""
#define ICON_LINK       ""
#define ICON_PERSON     ""
#define ICON_PEOPLE     ""
#define ICON_BRIEFCASE  ""
#define ICON_STAR       ""
#define ICON_CLOUD      ""
#define ICON_COPY       ""
#define ICON_CLOSE      ""
#define ICON_CHECK      ""
#define ICON_REFRESH    ""
#define ICON_SYNC       "\xEE\xA2\x95"  // U+E895: two circular arrows (update)
#define ICON_ADD        ""
#define ICON_DELETE     ""
#define ICON_FOLDER     ""
#define ICON_WARNING    ""
#define ICON_GLOBE      ""
#define ICON_DOCUMENT   ""
#define ICON_EYE        ""
#define ICON_FILTER     ""
#define ICON_CLOCK      ""

#define ICON_CHART        "\xEE\xA7\x92"  // U+E9D2 AreaChart
#define ICON_CHEVRON_DOWN "\xEE\x9C\x8D"  // U+E70D
#define ICON_CHEVRON_UP   "\xEE\x9C\x8E"  // U+E70E
#define ICON_GIFT         "\xEF\x84\xB3"  // U+F133 Giftbox
#define ICON_HIDE         "\xEE\xB4\x9A"  // U+ED1A Hide (crossed eye)
#define ICON_INFO         "\xEE\xA5\x86"  // U+E946 Info
#define ICON_LIGHTBULB    "\xEE\xAA\x80"  // U+EA80 Lightbulb
#define ICON_MAIL         "\xEE\x9C\x95"  // U+E715 Mail

namespace su::ui {

namespace color {
inline constexpr ImU32 Bg = IM_COL32(37, 39, 54, 255);
inline constexpr ImU32 Panel = IM_COL32(44, 46, 65, 255);
inline constexpr ImU32 PanelHover = IM_COL32(54, 57, 80, 255);
inline constexpr ImU32 PanelActive = IM_COL32(60, 63, 92, 255);
inline constexpr ImU32 Border = IM_COL32(62, 65, 90, 255);
inline constexpr ImU32 InputBg = IM_COL32(25, 26, 36, 255);
inline constexpr ImU32 Text = IM_COL32(232, 234, 242, 255);
inline constexpr ImU32 TextMuted = IM_COL32(150, 155, 180, 255);
inline constexpr ImU32 Accent = IM_COL32(99, 102, 241, 255);
inline constexpr ImU32 AccentHover = IM_COL32(118, 121, 250, 255);
inline constexpr ImU32 AccentActive = IM_COL32(84, 87, 214, 255);
inline constexpr ImU32 Link = IM_COL32(138, 160, 255, 255);
inline constexpr ImU32 Green = IM_COL32(34, 197, 94, 255);
inline constexpr ImU32 Blue = IM_COL32(56, 189, 248, 255);
inline constexpr ImU32 Orange = IM_COL32(245, 158, 11, 255);
inline constexpr ImU32 Red = IM_COL32(239, 68, 68, 255);
inline constexpr ImU32 Violet = IM_COL32(167, 139, 250, 255);

inline ImU32 withAlpha(ImU32 c, int alpha) { return (c & 0x00FFFFFFu) | (static_cast<ImU32>(alpha) << 24); }
inline ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
}  // namespace color

struct Fonts {
  ImFont* regular = nullptr;   // body text (Segoe UI)
  ImFont* semibold = nullptr;  // buttons, headings
  ImFont* caption = nullptr;   // table headers, captions
  ImFont* title = nullptr;     // brand / page titles
  ImFont* mono = nullptr;      // credentials, dates (Cascadia Mono / Consolas)
  ImFont* monoMedium = nullptr;  // stat values
  ImFont* monoBig = nullptr;   // big counters
};
Fonts& fonts();

// All icon glyphs.
inline constexpr const char* kAllIcons[] = {
    ICON_SEARCH, ICON_LOCK, ICON_UNLOCK, ICON_DOWNLOAD, ICON_BELL, ICON_CALENDAR, ICON_LINK, ICON_PERSON,
    ICON_PEOPLE, ICON_BRIEFCASE, ICON_STAR, ICON_CLOUD, ICON_COPY, ICON_CLOSE, ICON_CHECK, ICON_REFRESH,
    ICON_SYNC, ICON_ADD, ICON_DELETE, ICON_FOLDER, ICON_WARNING, ICON_GLOBE, ICON_DOCUMENT, ICON_EYE, ICON_FILTER, ICON_CLOCK,
    ICON_CHART, ICON_CHEVRON_DOWN, ICON_CHEVRON_UP, ICON_HIDE, ICON_GIFT,
    ICON_INFO, ICON_LIGHTBULB, ICON_MAIL};

}  // namespace su::ui
