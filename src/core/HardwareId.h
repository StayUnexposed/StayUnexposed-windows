#pragma once
#include <string>
#include <vector>

namespace su::hwid {

// Raw hardware facts used for the ID. Never shown or sent anywhere: only their hash is used.
struct Facts {
  std::string diskSerial;    // serial number of the physical disk holding the Windows volume
  std::string volumeSerial;  // serial of the Windows volume (fallback when the disk serial is unavailable)
  std::string mac;           // lowest MAC address among physical network adapters
};

Facts collect();

// Deterministic ID from the facts: "SU" + 10 Crockford base32 characters of a SHA-256 hash + one check
// character (Luhn mod 32), no dashes -- e.g. "SUT4BM2KNJC9H" (13 characters).
std::string idFromFacts(const Facts& f);

// Computed once per process.
const std::string& userId();

// Download link carrying the referral ID, e.g. https://stayunexposed.com/download?ref=SU-...
std::string referralUrl(const std::string& id);

// ---- Referral ID checksum ----
// Check character for 15 Crockford base32 characters (Luhn mod 32: catches any single wrong
// character and most swaps of two neighbouring characters).
char checkChar(const std::string& code);
// "SU" + code + check character (no dashes).
std::string withCheck(const std::string& code);
// Upper-cases, trims, drops spaces / dashes and applies Crockford's reading rules (O -> 0, I / L -> 1);
// returns the canonical "SU" + characters form (no dashes).
std::string normalizeId(const std::string& raw);
enum class IdCheck { Ok, BadFormat, BadChecksum };
IdCheck checkId(const std::string& normalizedId);

// Exposed for tests.
std::string sha256Hex(const std::string& data);
std::string base32(const std::vector<unsigned char>& bytes, size_t chars);

}  // namespace su::hwid
