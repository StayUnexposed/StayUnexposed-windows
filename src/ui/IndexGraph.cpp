#include "ui/IndexGraph.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace su::ui {

using namespace color;

namespace {

struct DatasetRef {
  const char* label;
  const DatasetStats* data;
  ImU32 color;
};

std::vector<DatasetRef> datasetsOf(const IndexStats& s) {
  std::vector<DatasetRef> out;
  if (s.leaks.present) out.push_back({"Stealer logs", &s.leaks, Accent});
  if (s.rawLines.present) out.push_back({"Raw lines", &s.rawLines, Green});
  return out;
}

// Top of the y axis: 4 x a round step (1, 1.5, 2, 2.5, 3, 4, 5, 6, 8 x 10^n) so grid labels stay short.
double niceTop(double maxValue) {
  const double v = maxValue / 4.0;
  if (v <= 0) return 4;
  const double p = std::pow(10.0, std::floor(std::log10(v)));
  for (double m : {1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0})
    if (v <= m * p) return 4.0 * m * p;
  return 40.0 * p;
}

const char* monthName(int m) {
  static const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  return (m >= 1 && m <= 12) ? names[m - 1] : "";
}

int monthOf(const std::string& ymd) { return ymd.size() >= 7 ? std::atoi(ymd.substr(5, 2).c_str()) : 0; }

void textAt(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 col, const char* text) {
  dl->AddText(font, font->FontSize, pos, col, text);
}

ImVec2 textSize(ImFont* font, const char* text) { return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, text); }

std::string withSign(int64_t v) { return (v > 0 ? "+" : "") + formatThousands(v); }

}  // namespace

float IndexGraph::height() const {
  const float fs = ImGui::GetFontSize();
  return collapsed_ ? fs * 3.2f : fs * 13.0f;
}

float IndexGraph::drawCounter(const LiveFigures& fig, ImVec2 at, bool compact) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFont* big = compact ? fonts().monoMedium : fonts().monoBig;
  const float lineH = big->FontSize;
  const double now = ImGui::GetTime();

  // ---- live dot: glowing core, two ripples spreading out, plus a burst when fresh figures arrive
  const ImVec2 dot(at.x + 6.0f, at.y + lineH * 0.5f);
  if (fig.available) {
    const float t = static_cast<float>(now);
    for (int k = 0; k < 2; ++k) {
      const float ph = std::fmod(t / 1.6f + k * 0.5f, 1.0f);  // 0..1, staggered half a period
      dl->AddCircle(dot, 5.0f + ph * 13.0f, withAlpha(Green, static_cast<int>(170 * (1.0f - ph) * (1.0f - ph))), 32, 1.6f);
    }
    if (fig.sinceFlash < 1.0f) {
      const float e = 1.0f - (1.0f - fig.sinceFlash) * (1.0f - fig.sinceFlash);  // ease-out
      dl->AddCircle(dot, 6.0f + e * 26.0f, withAlpha(Green, static_cast<int>(220 * (1.0f - fig.sinceFlash))), 40, 2.2f);
    }
    const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(now * 4.0));
    dl->AddCircleFilled(dot, 7.5f + pulse * 1.5f, withAlpha(Green, static_cast<int>(40 + 30 * pulse)), 24);
    dl->AddCircleFilled(dot, 4.5f, Green, 20);
    dl->AddCircleFilled(ImVec2(dot.x - 1.3f, dot.y - 1.3f), 1.5f, IM_COL32(220, 255, 230, 200), 10);
  } else {
    dl->AddCircleFilled(dot, 4.5f, withAlpha(Green, 80), 20);
  }

  const float x0 = at.x + 22.0f;
  const std::string value = fig.available ? formatThousands(static_cast<int64_t>(fig.lines)) : "-";
  textAt(dl, big, ImVec2(x0, at.y), Text, value.c_str());
  if (compact) {
    // one line: 832,446,146,891 indexed lines
    ImFont* cap = fonts().caption;
    textAt(dl, cap, ImVec2(x0 + textSize(big, value.c_str()).x + 8.0f, at.y + lineH - cap->FontSize - 1.0f), TextMuted,
           "indexed lines");
    return lineH;
  }
  // stacked: number, then "indexed lines  +12,514/s" underneath
  ImFont* small = fonts().regular;
  const float y2 = at.y + lineH + 2.0f;
  textAt(dl, small, ImVec2(x0, y2), TextMuted, "indexed lines");
  if (fig.available && fig.rate > 0) {
    const float rx = x0 + textSize(small, "indexed lines").x + 10.0f;
    const float beat = 0.75f + 0.25f * static_cast<float>(std::sin(now * 4.0));
    const std::string perSec = "+" + formatThousands(static_cast<int64_t>(fig.rate)) + "/s";
    textAt(dl, small, ImVec2(rx, y2), withAlpha(Green, static_cast<int>(255 * beat)), perSec.c_str());
    ImGui::SetCursorScreenPos(ImVec2(rx, y2));
    ImGui::InvisibleButton("##rate", ImVec2(textSize(small, perSec.c_str()).x, small->FontSize));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lines added per second, 24 h average. The counter follows this rate live.");
  }
  return lineH + 2.0f + small->FontSize;
}

void IndexGraph::draw(const std::optional<IndexStats>& stats, const std::optional<LiveStats>& live,
                      const LiveFigures& fig, bool loading) {
  const float fs = ImGui::GetFontSize();
  const float h = height();
  ImVec2 p = ImGui::GetCursorScreenPos();
  const float wdt = ImGui::GetContentRegionAvail().x;
  ImVec2 end(p.x + wdt, p.y + h);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, end, Panel, 12.0f);
  // Fresh live figures: the card border glows green and fades out over ~1.5 s.
  {
    const float flash = std::clamp(1.0f - fig.sinceFlash / 1.5f, 0.0f, 1.0f);
    const ImVec4 a = vec(Border), b = vec(Green);
    const float f = flash * 0.8f;
    dl->AddRect(p, end, ImGui::GetColorU32(ImVec4(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f, 1.0f)),
                12.0f, 0, 1.0f + flash);
  }

  const float pad = fs * 1.1f;
  const std::vector<DatasetRef> sets = stats ? datasetsOf(*stats) : std::vector<DatasetRef>{};
  if (dataset_ >= static_cast<int>(sets.size())) dataset_ = 0;
  const float headH = fs * 3.2f;  // heading row: room for the 2.2 fs button plus even padding
  const float tabH = fs + fs * 0.9f;
  ImFont* cap = fonts().caption;

  // Heading "[icon] Title  subtitle" vertically centred in the heading row; returns the x after it.
  auto heading = [&](float x, const char* icon, ImU32 iconCol, const char* title, const char* sub) {
    const float y = p.y + (headH - fs) * 0.5f;
    textAt(dl, fonts().regular, ImVec2(x, y), iconCol, icon);
    x += textSize(fonts().regular, icon).x + 8.0f;
    textAt(dl, fonts().semibold, ImVec2(x, y), Text, title);
    x += textSize(fonts().semibold, title).x + 8.0f;
    textAt(dl, fonts().regular, ImVec2(x, y), TextMuted, sub);
    return x + textSize(fonts().regular, sub).x;
  };

  // Hide / show toggle, always at the top right.
  // Expanded: a compact chevron (the heading row is busy); collapsed: "Show graph" with its label.
  const char* toggle = collapsed_ ? ICON_CHEVRON_DOWN "  Show graph" : ICON_CHEVRON_UP;
  const float tw = collapsed_ ? w::buttonWidth(toggle) : w::buttonHeight();
  const float toggleX = end.x - pad - tw;
  ImGui::SetCursorScreenPos(ImVec2(toggleX, p.y + (headH - w::buttonHeight()) * 0.5f));
  if (w::button(collapsed_ ? toggle : ICON_CHEVRON_UP "##hide_graph", w::ButtonKind::Ghost, ImVec2(tw, 0))) collapsed_ = !collapsed_;
  if (!collapsed_ && ImGui::IsItemHovered()) ImGui::SetTooltip("Hide graph");

  if (collapsed_) {
    // [chart] Index growth  last 12 months          (dot) 832,446,146,891 indexed lines   [Show graph]
    heading(p.x + pad, ICON_CHART, Violet, "Index growth", "last 12 months");
    const std::string value = fig.available ? formatThousands(static_cast<int64_t>(fig.lines)) : "-";
    const float cw = 22.0f + textSize(fonts().monoMedium, value.c_str()).x + 8.0f + textSize(cap, "indexed lines").x;
    ImGui::PushID("counter_compact");
    drawCounter(fig, ImVec2(toggleX - fs * 1.5f - cw, p.y + (headH - fonts().monoMedium->FontSize) * 0.5f), true);
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, end.y + 6.0f));
    ImGui::Dummy(ImVec2(0, 0));
    return;
  }

  // ---- three columns: [index growth] | [live values] | [raw lines added per hour]
  const float innerX0 = p.x + pad, innerX1 = end.x - pad;
  const float W = innerX1 - innerX0;
  const float colGap = fs * 1.3f;
  const float x1 = innerX0 + W * 0.39f;  // divider: growth | values
  const float x2 = x1 + W * 0.34f;       // divider: values | hourly
  const float bodyTop = p.y + headH;
  const float bodyBottom = end.y - pad * 0.8f;
  dl->AddLine(ImVec2(x1, p.y + pad), ImVec2(x1, bodyBottom), Border);
  dl->AddLine(ImVec2(x2, p.y + pad), ImVec2(x2, bodyBottom), Border);

  // Right: raw lines added per hour
  heading(x2 + colGap, ICON_CLOCK, Green, "Raw lines added", "per hour, last 24 h");
  if (live && live->sparkline24h.size() >= 2) {
    ImGui::PushID("hourly");
    drawHourly(*live, ImVec2(x2 + colGap, bodyTop), ImVec2(innerX1, bodyBottom));
    ImGui::PopID();
  } else {
    textAt(dl, cap, ImVec2(x2 + colGap, bodyTop), TextMuted, "Hourly figures not available.");
  }

  // Middle: live values, vertically centred
  {
    const float mx = x1 + colGap, mw = x2 - colGap - mx;
    const float counterH = fonts().monoBig->FontSize + 2.0f + fonts().regular->FontSize;
    const float statsH = fonts().monoMedium->FontSize + 2.0f + cap->FontSize;
    const float gapV = fs * 1.4f;
    float y = p.y + (h - (counterH + gapV + statsH)) * 0.5f;
    ImGui::PushID("counter");
    y += drawCounter(fig, ImVec2(mx, y), false) + gapV;
    ImGui::PopID();
    struct Stat { std::string value; const char* caption; const char* tip; };
    const Stat st[] = {
        {fig.available ? formatCompact(static_cast<double>(fig.leaks)) + "+" : "-", "leaks", "Leaked files in the index"},
        {fig.available ? formatCompact(fig.week) + "+" : "-", "this week", "Lines added in the last 7 days"},
        {fig.available ? formatThousands(static_cast<int64_t>(fig.rate * 60.0)) : "-", "lines/min",
         "Lines added per minute, 24 h average"},
    };
    const float sw = mw / 3.0f;
    for (int i = 0; i < 3; ++i) {
      const float x = mx + sw * static_cast<float>(i);
      textAt(dl, fonts().monoMedium, ImVec2(x, y), Text, st[i].value.c_str());
      textAt(dl, cap, ImVec2(x, y + fonts().monoMedium->FontSize + 2), TextMuted, st[i].caption);
      ImGui::SetCursorScreenPos(ImVec2(x, y));
      ImGui::PushID(i + 100);
      ImGui::InvisibleButton("stat", ImVec2(sw - 4, statsH));
      ImGui::PopID();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", st[i].tip);
    }
  }

  // Left: index growth (title, dataset tabs, figures, chart)
  {
    const float rx = innerX0;
    const float rEnd = x1 - colGap;
    float hx = heading(rx, ICON_CHART, Violet, "Index growth", "last 12 months");
    if (!sets.empty()) {
      std::vector<const char*> labels;
      for (const auto& s : sets) labels.push_back(s.label);
      ImGui::SetCursorScreenPos(ImVec2(hx + fs, p.y + (headH - tabH) * 0.5f));
      w::tabStrip("graph_sets", labels.data(), static_cast<int>(labels.size()), dataset_);
    }
    if (sets.empty()) {
      const char* msg = loading || !stats ? "Loading index statistics..." : "Index statistics are not available right now.";
      ImVec2 ts = ImGui::CalcTextSize(msg);
      dl->AddText(ImVec2((rx + rEnd - ts.x) * 0.5f, (bodyTop + bodyBottom - ts.y) * 0.5f), TextMuted, msg);
    } else {
      const DatasetStats& d = *sets[static_cast<size_t>(dataset_)].data;
      struct Fig { std::string value; const char* caption; int64_t exact; };
      const Fig figs[] = {
          {formatCompact(static_cast<double>(d.total)), "total", d.total},
          {"+" + formatCompact(static_cast<double>(d.today)), "today", d.today},
          {"+" + formatCompact(static_cast<double>(d.thisWeek)), "this week", d.thisWeek},
          {"+" + formatCompact(static_cast<double>(d.thisMonth)), "4 weeks", d.thisMonth},
      };
      float fx = rx;
      for (int i = 0; i < 4; ++i) {
        const float vw = textSize(fonts().semibold, figs[i].value.c_str()).x;
        const float itemW = vw + 5.0f + textSize(cap, figs[i].caption).x;
        if (fx + itemW > rEnd) break;
        textAt(dl, fonts().semibold, ImVec2(fx, bodyTop), i == 0 ? Text : Green, figs[i].value.c_str());
        textAt(dl, cap, ImVec2(fx + vw + 5.0f, bodyTop + fonts().semibold->FontSize - cap->FontSize - 1.0f), TextMuted,
               figs[i].caption);
        ImGui::SetCursorScreenPos(ImVec2(fx, bodyTop));
        ImGui::PushID(i);
        ImGui::InvisibleButton("fig", ImVec2(itemW, fonts().semibold->FontSize));
        ImGui::PopID();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s %s", formatThousands(figs[i].exact).c_str(), figs[i].caption);
        fx += itemW + fs * 1.1f;
      }
      ImGui::PushID("chart");
      seriesColor_ = sets[static_cast<size_t>(dataset_)].color;
      drawChart(d, ImVec2(rx, bodyTop + fs * 1.9f), ImVec2(rEnd, bodyBottom));
      ImGui::PopID();
    }
  }

  ImGui::SetCursorScreenPos(ImVec2(p.x, end.y + 6.0f));
  ImGui::Dummy(ImVec2(0, 0));
}

void IndexGraph::drawChart(const DatasetStats& d, ImVec2 a, ImVec2 b) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFont* cap = fonts().caption;

  // Series: running total at each week.
  std::vector<double> v;
  for (int64_t x : d.perWeek) v.push_back(static_cast<double>(x));
  if (v.size() < 2) {
    textAt(dl, cap, a, TextMuted, "Not enough data for a graph.");
    return;
  }

  const double top = niceTop(*std::max_element(v.begin(), v.end()));
  const float labelW = textSize(cap, "000.0B").x + 8.0f;
  const float xLabelH = cap->FontSize + 6.0f;
  const ImVec2 pa(a.x + labelW, a.y + cap->FontSize * 0.5f);
  const ImVec2 pb(b.x, b.y - xLabelH);
  const float pw = pb.x - pa.x, ph = pb.y - pa.y;
  if (pw < 40 || ph < 30) return;

  // Grid + y labels
  for (int g = 0; g <= 4; ++g) {
    const float y = pb.y - ph * static_cast<float>(g) / 4.0f;
    dl->AddLine(ImVec2(pa.x, y), ImVec2(pb.x, y), g == 0 ? Border : withAlpha(Border, 110));
    std::string lab = formatCompact(top * g / 4.0);
    ImVec2 ts = textSize(cap, lab.c_str());
    textAt(dl, cap, ImVec2(pa.x - 8.0f - ts.x, y - ts.y * 0.5f), TextMuted, lab.c_str());
  }

  const size_t count = v.size();
  auto xAt = [&](size_t i) { return pa.x + pw * static_cast<float>(i) / static_cast<float>(count - 1); };
  auto yAt = [&](double val) { return pb.y - ph * static_cast<float>(val / top); };

  // x labels: month name at the first week of each month, skipping when too close.
  float lastLabelEnd = -1e9f;
  int prevMonth = -1;
  for (size_t i = 0; i < count; ++i) {
    const std::string& wk = d.weeks[i];
    const int m = monthOf(wk);
    if (m == prevMonth) continue;
    prevMonth = m;
    std::string lab = monthName(m);
    if (m == 1 && wk.size() >= 4) lab += " " + wk.substr(2, 2);
    ImVec2 ts = textSize(cap, lab.c_str());
    const float x = xAt(i) - ts.x * 0.5f;
    if (x < lastLabelEnd + 10.0f || x + ts.x > pb.x + 10.0f) continue;
    textAt(dl, cap, ImVec2(x, pb.y + 5.0f), TextMuted, lab.c_str());
    lastLabelEnd = x + ts.x;
  }

  const ImU32 c = seriesColor_;
  // Area under the line, then the line.
  for (size_t i = 0; i + 1 < count; ++i) {
    ImVec2 p0(xAt(i), yAt(v[i])), p1(xAt(i + 1), yAt(v[i + 1]));
    dl->AddQuadFilled(p0, p1, ImVec2(p1.x, pb.y), ImVec2(p0.x, pb.y), withAlpha(c, 38));
  }
  std::vector<ImVec2> pts;
  for (size_t i = 0; i < count; ++i) pts.emplace_back(xAt(i), yAt(v[i]));
  dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), c, ImDrawFlags_None, 2.0f);

  // Hover: nearest week, crosshair + tooltip.
  ImGui::SetCursorScreenPos(pa);
  ImGui::InvisibleButton("plot", ImVec2(pw, ph));
  if (ImGui::IsItemHovered()) {
    const float mx = ImGui::GetIO().MousePos.x;
    const size_t i = static_cast<size_t>(std::clamp(std::lround((mx - pa.x) / pw * static_cast<float>(count - 1)), 0L,
                                                    static_cast<long>(count - 1)));
    const float x = xAt(i);
    dl->AddLine(ImVec2(x, pa.y), ImVec2(x, pb.y), withAlpha(Text, 70));
    dl->AddCircleFilled(ImVec2(x, yAt(v[i])), 4.5f, c);
    dl->AddCircle(ImVec2(x, yAt(v[i])), 4.5f, Panel, 0, 1.5f);
    const int64_t added = i > 0 ? d.perWeek[i] - d.perWeek[i - 1] : 0;
    ImGui::SetTooltip("Week of %s\n%s total\n%s that week", d.weeks[i].c_str(), formatThousands(d.perWeek[i]).c_str(),
                      withSign(added).c_str());
  }
}

void IndexGraph::drawHourly(const LiveStats& live, ImVec2 a, ImVec2 b) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFont* cap = fonts().caption;
  const auto& s = live.sparkline24h;
  const size_t n = s.size();

  // The heading ("Raw lines added per hour") is drawn by draw(); the bars start right at `a`.
  const ImVec2 pa(a.x, a.y + 4.0f);
  const ImVec2 pb(b.x, b.y - cap->FontSize - 6.0f);
  const float pw = pb.x - pa.x, ph = pb.y - pa.y;
  if (pw < 40 || ph < 20) return;

  const double top = std::max<double>(1.0, static_cast<double>(*std::max_element(s.begin(), s.end())));
  const float slot = pw / static_cast<float>(n);
  dl->AddLine(ImVec2(pa.x, pb.y), ImVec2(pb.x, pb.y), Border);
  ImGui::SetCursorScreenPos(pa);
  ImGui::InvisibleButton("hourly", ImVec2(pw, ph));
  const bool hovered = ImGui::IsItemHovered();
  const int hoverIdx = hovered ? std::clamp(static_cast<int>((ImGui::GetIO().MousePos.x - pa.x) / slot), 0,
                                            static_cast<int>(n) - 1)
                               : -1;
  for (size_t i = 0; i < n; ++i) {
    const float bh = std::max(2.0f, ph * static_cast<float>(static_cast<double>(s[i]) / top));
    const float x0 = pa.x + slot * static_cast<float>(i) + slot * 0.15f;
    const float x1 = pa.x + slot * static_cast<float>(i + 1) - slot * 0.15f;
    const bool last = i + 1 == n;
    ImU32 c = static_cast<int>(i) == hoverIdx ? Text : withAlpha(Green, last ? 255 : 170);
    dl->AddRectFilled(ImVec2(x0, pb.y - bh), ImVec2(x1, pb.y), c, 2.0f);
  }
  textAt(dl, cap, ImVec2(pa.x, pb.y + 4.0f), TextMuted, "24 h ago");
  const char* nowLab = "now";
  textAt(dl, cap, ImVec2(pb.x - textSize(cap, nowLab).x, pb.y + 4.0f), TextMuted, nowLab);
  if (live.sourcesActive > 0) {
    std::string src = formatThousands(live.sourcesActive) + " sources active";
    ImVec2 ts = textSize(cap, src.c_str());
    textAt(dl, cap, ImVec2((pa.x + pb.x - ts.x) * 0.5f, pb.y + 4.0f), withAlpha(TextMuted, 200), src.c_str());
  }

  if (hoverIdx >= 0) {
    // The last value is the current hour (UTC).
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_s(&utc, &now);
    const int hour = ((utc.tm_hour - (static_cast<int>(n) - 1 - hoverIdx)) % 24 + 24) % 24;
    const bool current = hoverIdx == static_cast<int>(n) - 1;
    ImGui::SetTooltip("%02d:00 UTC%s\n%s lines", hour, current ? " (current hour)" : "",
                      formatThousands(s[static_cast<size_t>(hoverIdx)]).c_str());
  }
}

}  // namespace su::ui
