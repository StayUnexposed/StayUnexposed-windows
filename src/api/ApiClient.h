#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "api/HttpClient.h"
#include "api/Models.h"

namespace su {

struct ApiError {
  int status = 0;        // HTTP status; 0 = network failure; -1 = not configured; -2 = bad response
  std::string message;   // user-facing text written by this app, never the server's text
};

template <class T>
struct Result {
  bool ok = false;
  T value{};
  ApiError error;

  static Result success(T v) { return Result{true, std::move(v), {}}; }
  static Result failure(ApiError e) { return Result{false, T{}, std::move(e)}; }
};

// Maps an HTTP status to a neutral, user-facing message.
std::string errorMessageFor(int status);

// Typed access to the search API. Thread-safe.
class ApiClient {
 public:
  ApiClient();

  bool configured() const {
#ifdef SU_MOCK
    return true;  // mock build serves sample data
#else
    return !host_.empty();
#endif
  }

  Result<LeakPage> searchIdentity(const std::string& value, IdKind kind, const std::string& filter, int page,
                                  int pageSize = 100);
  Result<DomainSummary> domainSummary(const std::string& domain);
  Result<LeakPage> domainLeaks(const std::string& domain, DomainCategory cat, const std::string& filter, IdKind kind,
                               int page, int pageSize = 100);
  Result<LeakPage> searchAdvanced(const AdvQuery& q, int page, int pageSize = 100);
  Result<DarkWebPage> searchDarkWeb(const std::string& query, int page, int pageSize = 25);
  Result<Usage> usage();
  Result<Profile> profile();
  Result<LiveStats> liveStats();
  Result<IndexStats> indexStats();

  // Unlocks stealer-log records by ID (1 point each) and returns them with their passwords.
  Result<std::vector<LeakRow>> unlockRows(const std::vector<std::string>& ids);


  // Aborts pending Retry-After waits (used at shutdown).
  void cancelAll() { cancelled_ = true; }

 private:
  Result<json> call(const wchar_t* method, const std::string& pathAndQuery, const json* body);

  std::wstring host_;
  std::unique_ptr<HttpClient> http_;
  std::mutex advancedGate_;  // the plan allows a limited number of concurrent advanced searches
  std::atomic<bool> cancelled_{false};
};

}  // namespace su
