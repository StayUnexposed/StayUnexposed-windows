#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/ApiClient.h"  // Result / ApiError

namespace su {

class HttpClient;

// Referral programme: a user earns $5 for every 20 users who install the app with their referral ID.
inline constexpr int kReferralsPerReward = 20;
inline constexpr double kRewardPerGroup = 5.0;  // dollars

struct ReferralEntry {
  std::string id;        // the referred user's ID (SU...)
  std::string joinedAt;  // ISO 8601, may be empty
};

struct ReferralStatus {
  std::vector<ReferralEntry> referrals;  // oldest first
  bool mock = false;                     // sample data (no referral API configured yet)
};

inline int64_t rewardGroups(size_t referralCount) { return static_cast<int64_t>(referralCount / kReferralsPerReward); }
inline double rewardDollars(size_t referralCount) { return static_cast<double>(rewardGroups(referralCount)) * kRewardPerGroup; }
inline int towardNextReward(size_t referralCount) { return static_cast<int>(referralCount % kReferralsPerReward); }
// "$1.00", "$12.00"
std::string formatDollars(double amount);

ReferralStatus parseReferralStatus(const nlohmann::json& j);

// Sample referrals shown until the referral API is ready.
ReferralStatus mockReferrals();

// Referral IDs: "SU" + 11 characters, the last character is a checksum (see hwid::checkId).
std::string normalizeReferralId(const std::string& s);
bool isReferralId(const std::string& s);  // format and checksum
// Empty when `raw` is a valid inviter ID for this user, otherwise the reason to show.
std::string referralIdProblem(const std::string& raw, const std::string& ownId);

// Talks to the referral API in kReferralApi (ReferralClient.cpp, the admin API):
//   POST {base}/register              {"referralId":"SU-..."}                 -> 201/409 (own ID saved)
//   POST {base}/referral/invite       {"referralId":"SU-...","invitedBy":"SU-..."} -> 200 (409 = already set)
//   GET  {base}/referral/{id}/invitees -> {"referrals":[{"id":"SU-...","joined_at":"..."}]} (404 = none yet)
// Only the hardware-derived referral ID (and the inviter's, when the user enters it) is sent. Thread-safe.
// When no API is configured the calls are no-ops (empty results, no sample data).
class ReferralClient {
 public:
  ReferralClient();
  ~ReferralClient();

  bool configured() const { return !host_.empty(); }
  // Registers this user's own referral ID with the server (once, on first run). Idempotent.
  Result<bool> registerSelf(const std::string& myId);
  // The users who joined with this user's referral ID.
  Result<ReferralStatus> fetch(const std::string& myId);
  // Records who invited this user (once).
  Result<bool> registerInviter(const std::string& myId, const std::string& invitedBy);

 private:
  std::wstring host_;
  std::string basePath_;  // no trailing slash
  std::unique_ptr<HttpClient> http_;
};

}  // namespace su
