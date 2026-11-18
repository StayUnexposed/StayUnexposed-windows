#include "services/Exporter.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <thread>

#include "core/Util.h"

namespace su {

std::string csvField(std::string_view v) {
  std::string s(v);
  // A cell starting with = + - @ (or tab/CR) is evaluated as a formula by spreadsheet apps.
  if (!s.empty() && (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@' || s[0] == '\t' || s[0] == '\r'))
    s.insert(s.begin(), '\'');
  bool quote = s.find_first_of(",\"\r\n") != std::string::npos;
  if (!quote) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += "\"\"";
    else out.push_back(c);
  }
  out += "\"";
  return out;
}

std::string csvLine(std::initializer_list<std::string_view> fields) {
  std::string line;
  bool first = true;
  for (auto f : fields) {
    if (!first) line.push_back(',');
    line += csvField(f);
    first = false;
  }
  line += "\r\n";
  return line;
}

std::filesystem::path downloadsPathFor(const std::string& baseName, const char* extension) {
  std::filesystem::path dir;
  PWSTR raw = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &raw))) dir = raw;
  CoTaskMemFree(raw);
  if (dir.empty()) dir = appDataDir();

  std::string safe;
  for (char c : baseName) {
    unsigned char u = static_cast<unsigned char>(c);
    safe.push_back((std::isalnum(u) || c == '.' || c == '-' || c == '_' || c == '@') ? c : '_');
  }
  if (safe.size() > 80) safe.resize(80);
  if (safe.empty()) safe = "export";

  std::filesystem::path p = dir / widen(safe + extension);
  for (int i = 2; std::filesystem::exists(p) && i < 1000; ++i)
    p = dir / widen(safe + " (" + std::to_string(i) + ")" + extension);
  return p;
}

bool Exporter::start(PageFn fn, int pageSize, int64_t total, const std::string& baseName) {
  auto shared = shared_;
  {
    std::lock_guard lock(shared->mutex);
    if (shared->status.running) return false;
    Status st;
    st.active = st.running = true;
    st.total = std::min(total, kMaxRows);
    st.path = downloadsPathFor(baseName, ".csv");
    st.fileName = narrow(st.path.filename().wstring());
    shared->status = st;
  }

  std::thread([shared, fn = std::move(fn), pageSize] {
    std::filesystem::path path;
    int64_t want;
    {
      std::lock_guard lock(shared->mutex);
      path = shared->status.path;
      want = shared->status.total;
    }
    auto fail = [&](const std::string& msg) {
      std::lock_guard lock(shared->mutex);
      shared->status.running = false;
      shared->status.failed = true;
      shared->status.error = msg;
    };

    std::ofstream out(path, std::ios::binary);
    if (!out) return fail("Can't write to the Downloads folder.");
    out << "\xEF\xBB\xBF";  // UTF-8 BOM so spreadsheet apps detect the encoding
    out << csvLine({"url", "type", "username", "password", "strength", "indexed_at", "info"});

    int64_t rows = 0;
    for (int page = 1; rows < want; ++page) {
      auto r = fn(page, pageSize);
      if (!r.ok) {
        out.close();
        return fail(r.error.message);
      }
      if (r.value.items.empty()) break;
      for (const auto& row : r.value.items) {
        if (rows >= want) break;
        bool email = row.isEmail.value_or(looksLikeEmail(row.username));
        std::string strength = row.strength ? std::to_string(*row.strength) : std::string();
        out << csvLine({row.url, email ? "email" : "username", row.username, row.password,
                        strength, row.addedAt, row.info});
        ++rows;
      }
      {
        std::lock_guard lock(shared->mutex);
        shared->status.rows = rows;
      }
      if (static_cast<int>(r.value.items.size()) < pageSize) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    out.close();
    std::lock_guard lock(shared->mutex);
    shared->status.rows = rows;
    shared->status.total = rows;
    shared->status.running = false;
  }).detach();
  return true;
}

Exporter::Status Exporter::status() const {
  std::lock_guard lock(shared_->mutex);
  return shared_->status;
}

void Exporter::dismiss() {
  std::lock_guard lock(shared_->mutex);
  if (!shared_->status.running) shared_->status = Status{};
}

void Exporter::markFinishedAt(double t) {
  std::lock_guard lock(shared_->mutex);
  if (!shared_->status.running && shared_->status.finishedAt == 0) shared_->status.finishedAt = t;
}

}  // namespace su
