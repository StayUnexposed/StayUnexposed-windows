#pragma once
#include <optional>
#include <string>
#include <vector>

// A referral download link (https://stayunexposed.com/download?ref=SU-...) hands out the app with the
// inviter's referral ID appended to the end of the .exe by the server (see admin /download). The app
// reads that stamp once, on first run, and registers it as the inviter the user cannot change.
//
// Trailer layout, at the very end of the file:
//   [ inviter id, ASCII, e.g. "SUT4BM2KNJC9H" ]
//   [ uint32 little-endian: length of that id ]
//   [ 8-byte magic kMagic ]
// Bytes appended after a PE's last section are ignored by Windows, so the app still runs. For a signed exe
// the server puts the stamp inside the certificate table (growing its size), which the signature's hash
// leaves out, so the signature stays valid; the stamp still ends the file.
namespace su::downloadstamp {

inline constexpr unsigned char kMagic[8] = {'S', 'U', 'R', 'E', 'F', '1', 0, 0};
inline constexpr size_t kMaxIdLen = 32;  // "SU" + 11 chars = 13

// The inviter id from a trailer at the end of `data` (normalised and checksum-valid), or nullopt when
// there is no valid stamp. `data` must end exactly where the file ends.
std::optional<std::string> parseStamp(const unsigned char* data, size_t size);

// `exeBytes` with a stamp for `inviterId` appended. The id is normalised; appends nothing and returns the
// input unchanged when the id is not a valid referral id.
std::vector<unsigned char> appendStamp(std::vector<unsigned char> exeBytes, const std::string& inviterId);

// The inviter stamped into the running .exe, or nullopt when it carries no valid stamp.
std::optional<std::string> readInviter();

}  // namespace su::downloadstamp
