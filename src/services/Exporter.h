#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "api/ApiClient.h"

namespace su {

// CSV field escaping (RFC 4180) with spreadsheet formula-injection protection.
std::string csvField(std::string_view value);
std::string csvLine(std::initializer_list<std::string_view> fields);

// Unique, filesystem-safe path inside the user's Downloads folder, e.g. leaks_acme-corp.com.csv
std::filesystem::path downloadsPathFor(const std::string& baseName, const char* extension);

// Background export of a paged search to CSV. One export at a time.
class Exporter {
 public:
  using PageFn = std::function<Result<LeakPage>(int page, int pageSize)>;

  static constexpr int64_t kMaxRows = 10000;

  struct Status {
    bool active = false;   // an export is shown (running or finished, not dismissed)
    bool running = false;
    bool failed = false;
    int64_t rows = 0;
    int64_t total = 0;     // expected rows (capped at kMaxRows)
    std::string fileName;
    std::filesystem::path path;
    std::string error;
    double finishedAt = 0; // seconds (ImGui time) when it finished, set by the UI
  };

  bool start(PageFn fn, int pageSize, int64_t total, const std::string& baseName);
  Status status() const;
  void dismiss();
  void markFinishedAt(double t);

 private:
  struct Shared {
    std::mutex mutex;
    Status status;
  };
  std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

}  // namespace su
