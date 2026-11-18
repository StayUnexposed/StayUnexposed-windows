#include "api/ApiClient.h"

#include "AppInfo.h"
#include "api/MockData.h"
#include "api/Secrets.h"
#include "core/Util.h"


namespace su {

namespace {
// Masked at compile time; see Secrets.h.
constexpr secrets::Obfuscated kHost{"api.***.***"};

constexpr int kMaxRetryAfterSec = 60;
constexpr int kDefaultRetryAfterSec = 5;

std::string pageQuery(int page, int pageSize) {
  return "page=" + std::to_string(page < 1 ? 1 : page) + "&page_size=" + std::to_string(pageSize);
}
}  // namespace

std::string errorMessageFor(int status) {
  switch (status) {
    case -1: return "This build has no API host configured.";
    case -2: return "The service returned an unexpected response.";
    case 0: return "Can't reach the service. Check your internet connection.";
    case 400:
    case 422: return "The search input was not accepted. Check the value and try again.";
    case 401: return "The service rejected this app's credentials.";
    case 403: return "This search isn't available on the current plan.";
    case 404: return "Nothing was found.";
    case 409: return "The request conflicted with another one. Try again.";
    case 429: return "Search limit reached. Try again later.";
    default:
      if (status >= 500) return "The service is temporarily unavailable. Try again later.";
      return "The request failed (code " + std::to_string(status) + ").";
  }
}

ApiClient::ApiClient()
    : host_(widen(kHost.reveal())), http_(std::make_unique<HttpClient>(APP_USER_AGENT_W)) {}

Result<json> ApiClient::call(const wchar_t* method, const std::string& pathAndQuery, const json* body) {
  if (!configured()) return Result<json>::failure({-1, errorMessageFor(-1)});

  std::wstring headers = L"Accept: application/json\r\n";
  std::string payload;
  if (body) {
    headers += L"Content-Type: application/json\r\n";
    payload = body->dump();
  }

  HttpResponse resp;
  for (int attempt = 0; attempt < 2; ++attempt) {
    resp = http_->send(host_, method, widen(pathAndQuery), headers, payload);
    if (resp.status != 429 || attempt == 1) break;
    // Rate limited: wait as instructed, retry exactly once (never in a loop).
    int wait = resp.retryAfterSec >= 0 ? resp.retryAfterSec : kDefaultRetryAfterSec;
    if (wait > kMaxRetryAfterSec) break;
    if (!sleepFor(wait * 1000, &cancelled_)) break;
  }

  if (resp.status >= 200 && resp.status < 300) {
    try {
      return Result<json>::success(json::parse(resp.body));
    } catch (const json::exception&) {
      return Result<json>::failure({-2, errorMessageFor(-2)});
    }
  }
  return Result<json>::failure({resp.status, errorMessageFor(resp.status)});
}

template <class T, class F>
static Result<T> mapResult(Result<json>&& r, F&& parse) {
  if (!r.ok) return Result<T>::failure(std::move(r.error));
  try {
    return Result<T>::success(parse(r.value));
  } catch (const json::exception&) {
    return Result<T>::failure({-2, errorMessageFor(-2)});
  }
}

Result<LeakPage> ApiClient::searchIdentity(const std::string& value, IdKind kind, const std::string& filter, int page,
                                           int pageSize) {
#ifdef SU_MOCK
  return Result<LeakPage>::success(mock::leakPage(page, pageSize));
#endif
  json body = {{"email", trim(value)}};
  if (!trim(filter).empty()) body["search"] = trim(filter);
  if (kind == IdKind::Email) body["is_email"] = true;
  if (kind == IdKind::Username) body["is_email"] = false;
  return mapResult<LeakPage>(call(L"POST", "/search/email?" + pageQuery(page, pageSize), &body),
                             [](const json& j) { return parseLeakPage(j); });
}

Result<DomainSummary> ApiClient::domainSummary(const std::string& domain) {
#ifdef SU_MOCK
  return Result<DomainSummary>::success(mock::domainSummary());
#endif
  return mapResult<DomainSummary>(call(L"GET", "/search/domain/" + urlEncode(trim(domain)), nullptr),
                                  [](const json& j) { return parseDomainSummary(j); });
}

Result<LeakPage> ApiClient::domainLeaks(const std::string& domain, DomainCategory cat, const std::string& filter,
                                        IdKind kind, int page, int pageSize) {
#ifdef SU_MOCK
  return Result<LeakPage>::success(mock::leakPage(page, pageSize));
#endif
  static const char* segments[] = {"employees", "customers", "third_parties", "all"};
  std::string path = "/search/domain/" + urlEncode(trim(domain)) + "/" + segments[static_cast<int>(cat)] + "?" +
                     pageQuery(page, pageSize);
  if (!trim(filter).empty()) path += "&search=" + urlEncode(trim(filter));
  if (kind == IdKind::Email) path += "&is_email=true";
  if (kind == IdKind::Username) path += "&is_email=false";
  return mapResult<LeakPage>(call(L"GET", path, nullptr), [cat](const json& j) {
    LeakPage p = parseLeakPage(j);
    static const char* labels[] = {"Employee", "Customer", "Third party", ""};
    for (auto& row : p.items)
      if (row.info.empty()) row.info = labels[static_cast<int>(cat)];
    return p;
  });
}

Result<LeakPage> ApiClient::searchAdvanced(const AdvQuery& q, int page, int pageSize) {
#ifdef SU_MOCK
  return Result<LeakPage>::success(mock::leakPage(page, pageSize));
#endif
  json body = buildAdvancedBody(q);
  std::lock_guard lock(advancedGate_);
  return mapResult<LeakPage>(call(L"POST", "/search/advanced?" + pageQuery(page, pageSize), &body),
                             [](const json& j) { return parseLeakPage(j); });
}

Result<Usage> ApiClient::usage() {
#ifdef SU_MOCK
  return Result<Usage>::success(mock::usage());
#endif
  return mapResult<Usage>(call(L"GET", "/profile/usage", nullptr), [](const json& j) { return parseUsage(j); });
}

Result<Profile> ApiClient::profile() {
#ifdef SU_MOCK
  return Result<Profile>::success(mock::profile());
#endif
  return mapResult<Profile>(call(L"GET", "/profile", nullptr), [](const json& j) { return parseProfile(j); });
}

Result<LiveStats> ApiClient::liveStats() {
#ifdef SU_MOCK
  return Result<LiveStats>::success(mock::liveStats());
#endif
  return mapResult<LiveStats>(call(L"GET", "/live-stats", nullptr), [](const json& j) { return parseLiveStats(j); });
}

Result<IndexStats> ApiClient::indexStats() {
#ifdef SU_MOCK
  return Result<IndexStats>::success(mock::indexStats());
#endif
  return mapResult<IndexStats>(call(L"GET", "/stats", nullptr), [](const json& j) { return parseIndexStats(j); });
}

Result<std::vector<LeakRow>> ApiClient::unlockRows(const std::vector<std::string>& ids) {
#ifdef SU_MOCK
  return Result<std::vector<LeakRow>>::success(mock::leakPage(1, 100).items);
#endif
  json body = {{"leak_ids", ids}};
  auto r = call(L"POST", "/unlock", &body);
  if (!r.ok && r.error.status == 400) r.error.message = "The password could not be unlocked.";
  return mapResult<std::vector<LeakRow>>(std::move(r), [](const json& j) {
    std::vector<LeakRow> rows;
    if (j.is_array())
      for (const auto& item : j) rows.push_back(parseStealerRow(item));
    return rows;
  });
}

Result<DarkWebPage> ApiClient::searchDarkWeb(const std::string& query, int page, int pageSize) {
#ifdef SU_MOCK
  return Result<DarkWebPage>::success(mock::darkWebPage(page, pageSize));
#endif
  json body = {{"query", trim(query)}};
  std::string path = "/search/dark-web?" + pageQuery(page, pageSize) + "&sort_by=published_at&sort_order=desc";
  return mapResult<DarkWebPage>(call(L"POST", path, &body), [](const json& j) { return parseDarkWebPage(j); });
}

}  // namespace su
