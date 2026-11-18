#include "services/ReferralClient.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <thread>

#include "AppInfo.h"
#include "api/HttpClient.h"
#include "core/HardwareId.h"
#include "core/Util.h"

static constexpr const char* kReferralApi = "https://admin.stayunexposed.com/api";

namespace su {

using json = nlohmann::json;

std::string formatDollars(double amount) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "$%.2f", amount);
  return buf;
}

ReferralStatus parseReferralStatus(const json& j) {
  ReferralStatus s;
  if (auto it = j.find("referrals"); it != j.end() && it->is_array()) {
    for (const auto& x : *it) {
      ReferralEntry e;
      if (x.is_string()) {
        e.id = x.get<std::string>();
      } else if (x.is_object()) {
        if (auto v = x.find("id"); v != x.end() && v->is_string()) e.id = v->get<std::string>();
        if (auto v = x.find("joined_at"); v != x.end() && v->is_string()) e.joinedAt = v->get<std::string>();
      }
      if (!e.id.empty()) s.referrals.push_back(std::move(e));
    }
  }
  return s;
}

ReferralStatus mockReferrals() {
  ReferralStatus s;
  for (const char* code : {"7QK2MZ4DH8", "C5RN0XJE9P", "2HTW8DGAKF", "9ZEP6BCVW1",
                           "M4FY3QHA7N", "JX61RT9WCB", "5VDNKQ7H2M"})
    s.referrals.push_back({hwid::withCheck(code), {}});
  s.mock = true;
  return s;
}

std::string normalizeReferralId(const std::string& s) { return hwid::normalizeId(s); }

bool isReferralId(const std::string& s) { return hwid::checkId(hwid::normalizeId(s)) == hwid::IdCheck::Ok; }

std::string referralIdProblem(const std::string& raw, const std::string& ownId) {
  const std::string id = hwid::normalizeId(raw);
  switch (hwid::checkId(id)) {
    case hwid::IdCheck::BadFormat:
      return "Referral IDs look like SUT4BM2KNJC9H (letters and digits).";
    case hwid::IdCheck::BadChecksum:
      return "This ID doesn't pass the check - a character is probably mistyped.";
    case hwid::IdCheck::Ok:
      break;
  }
  if (id == ownId) return "That's your own referral ID. Enter the ID of the person who invited you.";
  return {};
}

ReferralClient::ReferralClient() : http_(std::make_unique<HttpClient>(APP_USER_AGENT_W)) {
  std::string url = kReferralApi;
  const std::string scheme = "https://";
  if (url.rfind(scheme, 0) != 0) return;  // https only
  url.erase(0, scheme.size());
  const auto slash = url.find('/');
  host_ = widen(url.substr(0, slash));
  basePath_ = slash == std::string::npos ? std::string() : url.substr(slash);
  while (!basePath_.empty() && basePath_.back() == '/') basePath_.pop_back();
}

ReferralClient::~ReferralClient() = default;

Result<bool> ReferralClient::registerSelf(const std::string& myId) {
  using R = Result<bool>;
#ifdef SU_MOCK
  (void)myId;
  return R::success(true);
#endif
  if (!configured()) return R::success(true);
  const std::string body = json{{"referralId", myId}, {"appVersion", APP_VERSION_STR}}.dump();
  HttpResponse r = http_->send(host_, L"POST", widen(basePath_ + "/register"),
                               L"Content-Type: application/json\r\nAccept: application/json\r\n", body);
  if ((r.status >= 200 && r.status < 300) || r.status == 409) return R::success(true);  // 409 = already registered
  return R::failure({r.status, r.status == 0 ? "Can't reach the referral server." : "The referral server returned an error."});
}

Result<ReferralStatus> ReferralClient::fetch(const std::string& myId) {
  using R = Result<ReferralStatus>;
#ifdef SU_MOCK
  (void)myId;
  return R::success(mockReferrals());
#endif
  if (!configured()) return R::success({});  // no API configured: no referrals (and no sample data)
  HttpResponse r = http_->send(host_, L"GET", widen(basePath_ + "/referral/" + urlEncode(myId) + "/invitees"),
                               L"Accept: application/json\r\n", {});
  if (r.status == 404) return R::success({});  // not registered yet / nobody referred
  if (r.status < 200 || r.status >= 300)
    return R::failure({r.status, r.status == 0 ? "Can't reach the referral server." : "The referral server returned an error."});
  try {
    return R::success(parseReferralStatus(json::parse(r.body)));
  } catch (const json::exception&) {
    return R::failure({-2, "The referral server returned an unexpected response."});
  }
}

Result<bool> ReferralClient::registerInviter(const std::string& myId, const std::string& invitedBy) {
  using R = Result<bool>;
#ifdef SU_MOCK
  (void)myId;
  (void)invitedBy;
  return R::success(true);
#endif
  if (!configured()) return R::success(true);
  const std::string body = json{{"referralId", myId}, {"invitedBy", invitedBy}}.dump();
  HttpResponse r = http_->send(host_, L"POST", widen(basePath_ + "/referral/invite"),
                               L"Content-Type: application/json\r\nAccept: application/json\r\n", body);
  if (r.status >= 200 && r.status < 300) return R::success(true);
  if (r.status == 409) return R::failure({409, "An inviter is already registered for this PC."});
  if (r.status == 422) return R::failure({422, "That referral ID can't be used as your inviter."});
  return R::failure({r.status, r.status == 0 ? "Can't reach the referral server." : "The referral server returned an error."});
}

}  // namespace su
