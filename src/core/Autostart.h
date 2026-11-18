#pragma once

namespace su::autostart {

// Start-with-Windows via HKCU\...\Run (per user, no admin rights needed).
bool isEnabled();
bool setEnabled(bool enabled);
// If an entry from an earlier build (old product name) exists, replace it with the current one.
void migrateLegacy();

}  // namespace su::autostart
