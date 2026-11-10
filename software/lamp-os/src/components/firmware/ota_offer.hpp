#pragma once

#include <cstdint>
#include <vector>

#include <lampos/clock.hpp>

#include "components/network/mesh/lamp_roster.hpp"

namespace lamp {

// Anything that holds the offer scan back: the compositor's startup phase, an
// OTA moving in either direction for either image, or OTA quiet mode held by
// anything but the firmware distributor's own inheritable inter-session hold.
struct OtaOfferBusy {
  bool startingUp = false;
  bool firmwareSending = false;
  bool firmwareReceiving = false;
  bool fsBusy = false;
  bool quiet = false;
  bool firmwareIdleHold = false;

  bool any() const {
    return startingUp || firmwareSending || firmwareReceiving || fsBusy || quiet;
  }

  // An FS session started inside the firmware hold would inherit its visible
  // indicator and paint a progress bar for a silent update.
  bool fsWalkAllowed() const { return !firmwareIdleHold; }
};

// quietHolders is ota_quiet_mode::holders(). quietIsOwnIdleHold is the
// distributor's holdingIdleQuiet(): that hold exists so the next firmware offer
// arms inside it and inherits its quiet, so it alone never blocks the scan.
inline OtaOfferBusy offerBusy(bool startupComplete, bool firmwareSending,
                              bool firmwareReceiving, bool fsBusy, int quietHolders,
                              bool quietIsOwnIdleHold) {
  OtaOfferBusy busy;
  busy.startingUp = !startupComplete;
  busy.firmwareSending = firmwareSending;
  busy.firmwareReceiving = firmwareReceiving;
  busy.fsBusy = fsBusy;
  busy.quiet = quietHolders > (quietIsOwnIdleHold ? 1 : 0);
  busy.firmwareIdleHold = quietIsOwnIdleHold;
  return busy;
}

// Paces the offer scan. A busy lamp never scans and does not spend the interval.
class OtaOfferThrottle {
 public:
  // Lamps emit HELLO every 1-2 s, so 1 s catches every fresh sighting while
  // keeping the roster snapshot off the per-frame path.
  static constexpr uint32_t kIntervalMs = 1000;

  // True when a scan should run at nowMs; stamps nowMs as the last scan. Call
  // at least once per lampos::kRetiredAgeMs, busy or not, so a stale stamp
  // retires.
  bool due(const OtaOfferBusy& busy, uint32_t nowMs) {
    if (lastScanMs_ != 0 && lampos::msSince(nowMs, lastScanMs_) >= kIntervalMs) lastScanMs_ = 0;
    if (busy.any()) return false;
    if (lastScanMs_ != 0 && nowMs - lastScanMs_ < kIntervalMs) return false;
    lastScanMs_ = nowMs;
    return true;
  }

 private:
  uint32_t lastScanMs_ = 0;
};

// True when a reachable peer runs firmware newer than ownVersion. This lamp is
// about to receive, so anything it sent now would be re-sent under the new image.
inline bool peerRunsNewerFirmware(const std::vector<MeshPeer>& peers, uint32_t ownVersion) {
  for (const auto& p : peers) {
    if (p.hasMac && p.firmwareVersion > ownVersion) return true;
  }
  return false;
}

// Offers this lamp's firmware and filesystem image to stale peers once
// startupComplete (the compositor's startup phase has ended). Runs every loop
// on every variant, whatever the lamp's features, home mode or social mode.
// Version updates outrank social preference: personality shapes the
// greeting, never whether firmware propagates.
void tickOtaOffers(uint32_t nowMs, bool startupComplete);

}  // namespace lamp
