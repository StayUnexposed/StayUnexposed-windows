#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace su::signature {

// Who signed a file, read from its Authenticode signature.
struct Signer {
  std::string subject;     // the certificate's display name (e.g. "Stay Unexposed"), UTF-8
  std::string thumbprint;  // SHA-1 of the certificate, upper-case hex
};

// The signer of `file` when it carries a valid Authenticode signature that chains to a root trusted on this
// PC; nullopt when it is unsigned, tampered with, or its certificate is not trusted. Revocation is not checked
// online, so this works offline and never waits on the network.
std::optional<Signer> verifiedSigner(const std::filesystem::path& file);

// The running exe's signer (computed once).
const std::optional<Signer>& ownSigner();

// Update rule: once the running app is signed, an update must be signed by the same publisher
// (same certificate subject, so a renewed certificate still matches). An unsigned app accepts any update,
// so the first signed release can still reach users of the earlier, unsigned versions.
bool acceptableUpdateSigner(const std::optional<Signer>& running, const std::optional<Signer>& update);

}  // namespace su::signature
