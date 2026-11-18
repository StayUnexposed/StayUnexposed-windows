#include "ui/Style.h"

#include <windows.h>

#include <imgui.h>

#include <string>

#include "core/Util.h"
#include "ui/Theme.h"

namespace su::ui {

Fonts& fonts() {
  static Fonts f;
  return f;
}

namespace {

std::string fontPath(const wchar_t* file) {
  wchar_t winDir[MAX_PATH];
  GetWindowsDirectoryW(winDir, MAX_PATH);
  std::wstring path = std::wstring(winDir) + L"\\Fonts\\" + file;
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
  return narrow(path);
}

std::string firstFont(std::initializer_list<const wchar_t*> files) {
  for (auto f : files)
    if (auto p = fontPath(f); !p.empty()) return p;
  return {};
}

const ImWchar* textRanges() {
  static ImVector<ImWchar> ranges;
  if (ranges.empty()) {
    ImFontGlyphRangesBuilder builder;
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    builder.AddRanges(atlas->GetGlyphRangesDefault());
    builder.AddRanges(atlas->GetGlyphRangesCyrillic());
    builder.AddRanges(atlas->GetGlyphRangesGreek());
    static const ImWchar extra[] = {0x0100, 0x024F, 0x2010, 0x2027, 0x2039, 0x203A, 0x2190, 0x2193, 0x25CF, 0x25CF, 0};
    builder.AddRanges(extra);
    builder.BuildRanges(&ranges);
  }
  return ranges.Data;
}

// Loads `file` at `size` and merges the Windows icon font into it.
ImFont* load(const std::string& file, float size) {
  ImGuiIO& io = ImGui::GetIO();
  ImFont* font = nullptr;
  if (!file.empty()) font = io.Fonts->AddFontFromFileTTF(file.c_str(), size, nullptr, textRanges());
  if (!font) {
    ImFontConfig cfg;
    cfg.SizePixels = size;
    font = io.Fonts->AddFontDefault(&cfg);
  }
  static const std::string icons = firstFont({L"SegoeIcons.ttf", L"segmdl2.ttf"});
  if (!icons.empty()) {
    static const ImWchar iconRanges[] = {0xE700, 0xF8FF, 0};
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.GlyphOffset = ImVec2(0, size * 0.16f);
    cfg.GlyphMinAdvanceX = size;  // fixed-width icons align nicely
    io.Fonts->AddFontFromFileTTF(icons.c_str(), size * 0.92f, &cfg, iconRanges);
  }
  return font;
}

}  // namespace

void setupStyle(float scale) {
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // don't write imgui.ini next to the exe
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.ChildRounding = 10.0f;
  style.FrameRounding = 8.0f;
  style.GrabRounding = 6.0f;
  style.TabRounding = 6.0f;
  style.PopupRounding = 10.0f;
  style.ScrollbarRounding = 8.0f;
  style.ScrollbarSize = 12.0f;
  style.FramePadding = ImVec2(10, 7);
  style.ItemSpacing = ImVec2(10, 6);
  style.ItemInnerSpacing = ImVec2(8, 6);
  style.CellPadding = ImVec2(14, 8);
  style.WindowPadding = ImVec2(20, 16);
  style.PopupBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;

  using namespace color;
  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = vec(Text);
  c[ImGuiCol_TextDisabled] = vec(TextMuted);
  c[ImGuiCol_WindowBg] = vec(Bg);
  c[ImGuiCol_ChildBg] = vec(withAlpha(Bg, 0));
  c[ImGuiCol_PopupBg] = vec(Panel);
  c[ImGuiCol_Border] = vec(Border);
  c[ImGuiCol_FrameBg] = vec(InputBg);
  c[ImGuiCol_FrameBgHovered] = vec(IM_COL32(32, 34, 47, 255));
  c[ImGuiCol_FrameBgActive] = vec(IM_COL32(36, 38, 54, 255));
  // Plain ImGui buttons (combo arrows, small buttons) stay neutral; primary buttons are drawn by w::button.
  c[ImGuiCol_Button] = vec(PanelHover);
  c[ImGuiCol_ButtonHovered] = vec(PanelActive);
  c[ImGuiCol_ButtonActive] = vec(withAlpha(Accent, 160));
  c[ImGuiCol_Header] = vec(withAlpha(Accent, 90));
  c[ImGuiCol_HeaderHovered] = vec(withAlpha(Accent, 120));
  c[ImGuiCol_HeaderActive] = vec(withAlpha(Accent, 160));
  c[ImGuiCol_CheckMark] = vec(Accent);
  c[ImGuiCol_SliderGrab] = vec(Accent);
  c[ImGuiCol_Separator] = vec(Border);
  c[ImGuiCol_TableHeaderBg] = vec(Panel);
  c[ImGuiCol_TableBorderStrong] = vec(Border);
  c[ImGuiCol_TableBorderLight] = vec(Border);
  c[ImGuiCol_TableRowBg] = vec(withAlpha(Panel, 0));
  c[ImGuiCol_TableRowBgAlt] = vec(withAlpha(Panel, 0));
  c[ImGuiCol_ScrollbarBg] = vec(withAlpha(Bg, 0));
  c[ImGuiCol_ScrollbarGrab] = vec(Border);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.05f, 0.08f, 0.6f);
  c[ImGuiCol_PlotHistogram] = vec(Green);
  style.ScaleAllSizes(scale);

  const std::string ui = firstFont({L"segoeui.ttf"});
  const std::string uiSemibold = firstFont({L"seguisb.ttf", L"segoeuib.ttf", L"segoeui.ttf"});
  const std::string mono = firstFont({L"CascadiaMono.ttf", L"consola.ttf", L"cour.ttf"});
  const std::string monoBold = firstFont({L"consolab.ttf", L"CascadiaMono.ttf", L"consola.ttf"});

  Fonts& f = fonts();
  f.regular = load(ui, 16.0f * scale);  // first font = default
  f.semibold = load(uiSemibold, 16.0f * scale);
  f.caption = load(uiSemibold, 13.0f * scale);
  f.title = load(uiSemibold, 19.0f * scale);
  f.mono = load(mono, 14.5f * scale);
  f.monoMedium = load(monoBold, 16.0f * scale);
  f.monoBig = load(monoBold, 26.0f * scale);
}

}  // namespace su::ui
