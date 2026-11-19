#include "components/firmware/ota_offer.hpp"

#include <cstring>

#include "components/firmware/firmware_distributor.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "components/firmware/fs_ota.hpp"
#include "components/firmware/ota_mark_valid.hpp"
#include "components/firmware/ota_quiet_mode.hpp"
#include "components/webapp/webapp.hpp"
#include "version.hpp"

// Defined at file scope in lamp.cpp; the receiver header exposes no extern.
extern lamp::FirmwareReceiver firmwareReceiver;

namespace lamp {

namespace {
OtaOfferThrottle s_throttle;
}  // namespace

// The distributor's per-peer backoff makes a repeat considerPeerForOta on
// every scan safe.
//
// Only ESP-NOW peers are walked, since firmwareVersion arrives on the ESP-NOW
// HELLO and a BLE-only sighting carries none.
void tickOtaOffers(uint32_t nowMs, bool startupComplete) {
#if LAMP_WEBAPP_ENABLED
  const bool webClient = webapp::hasClient();
#else
  const bool webClient = false;
#endif
  const OtaOfferBusy busy =
      offerBusy(startupComplete, firmwareDistributor.isInProgress(),
                ::firmwareReceiver.isInProgress(), fs_ota::fsPathBusy(),
                ota_quiet_mode::holders(), firmwareDistributor.holdingIdleQuiet(),
                otaMarkValid.pending(), webClient);
  if (!s_throttle.due(busy, nowMs)) return;

  const std::vector<MeshPeer>& peers = lampRoster.getMesh(LAMP_PRUNE_TIME_MS);
  if (peerRunsNewerFirmware(peers, FIRMWARE_VERSION)) return;

  for (const auto& p : peers) {
    if (!p.hasMac) continue;
    if (p.firmwareVersion == 0) continue;
    // Don't stomp a transfer another lamp already owns: the peer is
    // receiving, or some roster peer reports sending to it. The roster holds
    // peers only, so this lamp's own in-progress send never trips the scan.
    bool peerBeingServed = p.otaState == lamp_protocol::kOtaStateReceiving;
    if (!peerBeingServed) {
      for (const auto& q : peers) {
        if (q.otaState == lamp_protocol::kOtaStateSending && q.hasOtaSendingTo &&
            std::memcmp(q.otaSendingTo, p.mac, 6) == 0) {
          peerBeingServed = true;
          break;
        }
      }
    }
    firmwareDistributor.considerPeerForOta(p.mac, p.firmwareVersion, p.protocolVersion, nowMs,
                                           p.fwChannel, p.maxChunk, p.otaLinkHeld,
                                           peerBeingServed);
  }
  if (!busy.fsWalkAllowed()) return;
  // fs_ota::considerPeer does its own staleness and busy gating.
  for (const auto& p : peers) {
    if (!p.hasMac) continue;
    fs_ota::considerPeer(p.mac, p.firmwareVersion, p.protocolVersion, nowMs, p.fwChannel,
                         p.fsDigest, p.hasFsDigest, p.needsFs, p.otaLinkHeld);
  }
}

}  // namespace lamp
