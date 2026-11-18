#pragma once
#include <string>

#include "api/Models.h"

namespace su::ui {

// Advanced search builder: include / exclude values per field (chips), match type per field,
// identifier type, password strength, indexed date range and the AND switch.
class AdvancedForm {
 public:
  // Draws the whole form. Returns true when Search was pressed with at least one filter.
  bool draw();
  const AdvQuery& query() const { return q_; }
  // "3 values in 2 fields: URL domain, Password" / "No filter yet"
  std::string summary() const;
  void clear();
  void loadSampleData();  // sample filters

 private:
  static constexpr int kFields = static_cast<int>(AdvField::Count);
  bool fieldRow(AdvField f, float labelW, float colW);
  bool tagBox(AdvField f, bool exclude, float width);
  void commit(AdvField f, bool exclude, bool all);
  void drawHelp();

  AdvQuery q_;
  std::string buf_[kFields][2];  // text being typed (0 include, 1 exclude)
  std::string err_[kFields][2];  // last rejected value's reason
  bool examples_ = false;
};

}  // namespace su::ui
