#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

#include "api/Models.h"

namespace su::ui {

struct TableOptions {
  bool showUrl = true;
  const char* infoLabel = nullptr;  // extra column (category / source); nullptr = hidden
  double rowDelay = 0.0;            // eye on a row: seconds of "Unlocking..." before the password appears
  int unlockLimit = 0;              // locked rows with index < limit get an eye that unlocks them
  const std::unordered_set<std::string>* unlocking = nullptr;  // IDs queued / being unlocked
};

// Leak table styled like the web dashboard. Passwords are hidden behind dots; the eye button on a
// row shows/hides it. Returns the index of a locked row whose eye was clicked (caller unlocks it), or -1.
// `height` 0 = fill remaining space.
int drawResultsTable(const char* id, const std::vector<LeakRow>& rows, const TableOptions& opt, float height = 0.0f);

// Rows opened with the per-row eye: leakKey -> time (ImGui::GetTime) the password appears. Shared by every table.
std::unordered_map<std::string, double>& revealedRows();

// "‹  Page 2 / 13  ›". Returns true and updates `page` when the user navigates.
bool drawPager(const char* id, int& page, int64_t total, int pageSize, bool disabled);

// Table chrome shared by every list: no vertical lines, transparent header, rounded frame.
int tableFlags();
float tableRowHeight();
// Height that fits `rows` rows (plus header) or 0 (= fill remaining space) when that's taller.
float tableFitHeight(size_t rows);
void tableBegin();                // push colours (call before BeginTable)
void tableEnd(bool tableDrawn);   // pop colours + rounded border (call after EndTable)

// Small helpers shared by views
void helpMarker(const char* text);
void errorText(const std::string& text);
void mutedText(const std::string& text);
void spinner(const char* label);

}  // namespace su::ui
