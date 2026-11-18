#pragma once
#include <imgui.h>

#include <optional>

#include "api/Models.h"

namespace su::ui {

// Live index figures as currently displayed (computed by App each frame).
struct LiveFigures {
  bool available = false;
  double lines = 0;        // indexed lines, advancing at `rate`
  double rate = 0;         // lines per second (24 h average)
  double week = 0;         // lines added this week
  int64_t leaks = 0;       // leaked files
  float sinceFlash = 1e9f; // seconds since fresh figures arrived
};

// "Index growth" card: live indexed-lines counter and figures, weekly growth of each dataset over the
// last year (running total) and lines added per hour over the last 24 hours.
class IndexGraph {
 public:
  void draw(const std::optional<IndexStats>& stats, const std::optional<LiveStats>& live, const LiveFigures& fig,
            bool loading);
  float height() const;  // height draw() will take

 private:
  void drawChart(const DatasetStats& d, ImVec2 a, ImVec2 b);
  void drawHourly(const LiveStats& live, ImVec2 a, ImVec2 b);
  // Pulsing dot + big counter + "indexed lines" + "+N/s"; returns the height used.
  float drawCounter(const LiveFigures& fig, ImVec2 at, bool compact);

  int dataset_ = 0;  // index into the datasets present
  bool collapsed_ = false;
  ImU32 seriesColor_ = 0;  // colour of the selected dataset
};

}  // namespace su::ui
