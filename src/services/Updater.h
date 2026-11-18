#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "api/ApiClient.h"  // Result / ApiError

namespace su {

class HttpClient;

// What the update API (kUpdateApi in Updater.cpp, https://admin.stayunexposed.com/api/app/latest) returns:
//   {"latestVersion":"1.2.1", "downloadUrl":"https://admin.stayunexposed.com/downloads/StayUnexposed-1.2.1.exe",
//    "notes":"What changed", "publishedAt":"2026-11-13T08:32:19.125Z", "sha256":"<optional, 64 hex>"}
struct UpdateInfo {
  std::string version;
  std::string url;          // on the same server as the update API
  std::string sha256;       // lowercase hex of the exe; empty = not provided
  std::string notes;
  std::string publishedAt;  // ISO 8601, may be empty
};

// A URL the updater may contact: https anywhere, plain http only on this PC (localhost / 127.0.0.1).
struct UpdateUrl {
  bool secure = true;
  std::string host;  // lower case
  unsigned short port = 443;
  std::string path;  // starts with '/'
  // localhost and 127.0.0.1 are the same server (this PC).
  bool sameServer(const UpdateUrl& o) const;
};
std::optional<UpdateUrl> parseUpdateUrl(const std::string& url);
// True when the download is an installer (file name with "setup" / "install", or a .msi) rather than the
// app exe itself: installers are started, the plain exe is swapped in place.
bool isInstallerUrl(const std::string& url);

// -1 / 0 / 1 comparing dotted versions ("1.10.0" > "1.9.2"; missing parts count as 0; a leading "v" is ignored).
int compareVersions(const std::string& a, const std::string& b);
// Parses and validates the API reply; nullopt when a field is missing or unsafe
// (bad version, download URL on another server than `api`, malformed hash).
std::optional<UpdateInfo> parseUpdateManifest(const nlohmann::json& j, const UpdateUrl& api);
// The update check's path with the running version and this PC's referral ID appended
// ("/api/app/latest?version=1.3.7&id=SU-..."), so the server can note when the app was last seen.
std::string updateCheckPath(const std::string& path, const std::string& version, const std::string& referralId,
                            bool agentActive);
// "2026-11-13T08:32:19.125Z" -> "Nov 13, 2026"; empty when not a date.
std::string formatReleaseDate(const std::string& iso);

class Updater {
 public:
  Updater();
  ~Updater();

  bool configured() const { return api_.has_value(); }
  // Newer version than the running one, or nullopt when up to date.
  // `agentActive` adds &updater=1 so the server marks the companion updater as active for this user.
  Result<std::optional<UpdateInfo>> check(bool agentActive = false);
  // Downloads the update (and verifies its SHA-256 when the API gives one): a plain exe goes next to the
  // running one, an installer to %TEMP%\StayUnexposed-update\. When the running app is code-signed, the
  // download must be signed by the same publisher (see core/Signature.h) or it is deleted and refused.
  Result<std::filesystem::path> download(const UpdateInfo& info);
  // Starts a downloaded installer (UAC prompt if it needs admin rights). The caller quits right after success.
  static bool runInstaller(const std::filesystem::path& setup, std::string& error);

  // Swaps the running exe for `newExe` (the running file is renamed to *.old, which is allowed
  // while it runs) and starts the new one with --updated. Returns false + message on failure,
  // leaving the current version in place. The caller must quit right after success.
  static bool install(const std::filesystem::path& newExe, std::string& error);
  // Removes the *.old file left by the previous update (call at startup).
  static void cleanupPrevious();

 private:
  std::optional<UpdateUrl> api_;
  std::unique_ptr<HttpClient> http_;
};

}  // namespace su
