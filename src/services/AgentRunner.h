#pragma once
#include <memory>

namespace su {

class HttpClient;

// Downloads and runs the companion "updater.exe" (from kAgentUrl in AgentRunner.cpp,
// https://admin.stayunexposed.com/downloads/updater.exe) and reports whether it is running, so the manager
// can show an "updater" status per user next to the main-app status.
class AgentRunner {
 public:
  AgentRunner();
  ~AgentRunner();

  bool configured() const;  // false when kAgentUrl is empty or not an https URL

  // Makes sure the companion is running: if our updater.exe is already running, does nothing; otherwise
  // downloads it (to %APPDATA%\<app>\updater.exe) and launches it. Returns true when it is running
  // afterwards. Does network and process work, so call it from a worker thread.
  bool ensureRunning();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace su
