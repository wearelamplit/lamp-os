#include "components/firmware/fs_ota.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_partition.h>

#include <cstring>
#include <string>
#include <vector>

#include "components/firmware/firmware_distributor.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "components/firmware/fs_signature.hpp"
#include "components/network/protocol/lamp_protocol.hpp"
#include "components/webapp/webapp.hpp"
#include "version.hpp"

namespace fs_ota {
namespace {

namespace lp = lamp_protocol;

// Upper bound on a single SPIFFS asset (the partition is 192 KB). Guards the
// boot-time enumeration against a bogus file size aborting a vector resize.
constexpr size_t kMaxAssetBytes = 262144;

// The FS receiver + distributor reuse the firmware OTA engine via hooks; they
// target the spiffs partition and emit MSG_FS_*.
lamp::FirmwareReceiver    s_fsReceiver;
lamp::FirmwareDistributor s_fsDistributor;

// Firmware-path instances, for the cross-OTA busy guard. Set in begin().
lamp::FirmwareReceiver*    s_fwRecv = nullptr;
lamp::FirmwareDistributor* s_fwDist = nullptr;

const esp_partition_t* s_spiffsPart   = nullptr;
uint8_t                s_localDigest[32] = {0};
bool                   s_localDigestReady = false;
// The fw.lsig ed25519 signature over the manifest digest, carried in the OFFER
// auth trailer so a receiver verifies before erasing its live web UI.
uint8_t                s_localSignature[64] = {0};
bool                   s_localSigReady = false;
// Version stamped in fw.lsig. Distribution self-gate: only OFFER the FS
// image when this equals the running firmware version (a firmware-OTA'd lamp
// whose SPIFFS is still the stale older image must NOT push it back, or it
// corrupts the seed before the receiver's verify can reject it).
uint32_t               s_localFsVersion = 0;

// SPIFFS must be mounted. Fills `files` (excluding fw.lsig) with STREAMING
// readers (each re-opens its file per read) and `lsig` with the fw.lsig bytes.
// Returns true iff fw.lsig was present. Streaming, not whole-file buffering, is
// deliberate: a ~44 KB contiguous vector resize can fail under heap
// fragmentation (WiFi + BLE + AsyncWebServer all resident) and abort() under
// -fno-exceptions, a boot-time crash loop. Re-opening per 4 KB read is slow
// but allocation-free and only runs once at boot / once post-OTA.
bool enumerateManifest(std::vector<lamp::firmware::FsManifestFile>& files,
                       std::vector<uint8_t>& lsig) {
  File root = SPIFFS.open("/");
  if (!root) return false;
  bool haveLsig = false;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    // Canonical name: strip any leading '/' so it matches sign_fs.py's
    // basenames (the host/device agreement hinge).
    const char* raw = f.name();
    std::string name = raw ? raw : "";
    if (!name.empty() && name[0] == '/') name.erase(0, 1);
    const size_t sz = f.size();
    // A real UI asset can't be empty or exceed the partition; skip ghosts.
    if (name.empty() || sz == 0 || sz > kMaxAssetBytes) continue;
    if (name == lamp::firmware::kFsSigName) {
      lsig.resize(sz);
      if (f.read(lsig.data(), sz) != static_cast<int>(sz)) return false;
      haveLsig = true;
      continue;
    }
    const std::string path = "/" + name;
    lamp::firmware::FsManifestFile mf;
    mf.name = name;
    mf.contentLen = sz;
    mf.read = [path](size_t off, size_t want, uint8_t* out) -> int {
      File ff = SPIFFS.open(path.c_str(), "r");
      if (!ff) return -1;
      if (!ff.seek(off)) { ff.close(); return -1; }
      const int n = ff.read(out, want);
      ff.close();
      return n;
    };
    files.push_back(std::move(mf));
  }
  return haveLsig;
}

void computeLocalDigest() {
  s_localDigestReady = false;
  s_localSigReady = false;
  s_localFsVersion = 0;
  if (!SPIFFS.begin(/*formatOnFail=*/false)) return;
  std::vector<lamp::firmware::FsManifestFile> files;
  std::vector<uint8_t> lsig;
  if (!enumerateManifest(files, lsig)) return;
  if (lsig.size() == lamp::firmware::kFsSigLen) {
    const uint8_t* v = lsig.data() + lamp::firmware::kFsSigVersionOffset;
    s_localFsVersion = static_cast<uint32_t>(v[0]) |
                       (static_cast<uint32_t>(v[1]) << 8) |
                       (static_cast<uint32_t>(v[2]) << 16) |
                       (static_cast<uint32_t>(v[3]) << 24);
    std::memcpy(s_localSignature,
                lsig.data() + lamp::firmware::kFsSigSignatureOffset,
                lamp::firmware::kFsSigSignatureLen);
    s_localSigReady = true;
  }
  if (lamp::firmware::computeFsManifestDigest(files, s_localDigest)) {
    s_localDigestReady = true;
  }
}

// Unmount SPIFFS before the receiver erases+writes the partition raw (a
// mounted SPIFFS would corrupt on raw writes), then hand it the partition.
const void* fsRecvPartition() {
  SPIFFS.end();
  return static_cast<const void*>(s_spiffsPart);
}

bool fsShouldAccept(uint32_t offerVersion, const uint8_t* offerDigestPrefix) {
  // Don't overwrite the UI partition while the web server is mid-serve.
  if (webapp::isActive()) return false;
  // Version-coupled: only an FS image stamped with the running firmware
  // version, and only if its content differs from the local image.
  if (offerVersion != lamp::FIRMWARE_VERSION) return false;
  if (!s_localDigestReady) return true;  // no local image → take anything at this version
  return std::memcmp(offerDigestPrefix, s_localDigest,
                     lp::FW_SHA256_PREFIX_LEN) != 0;
}

lp::FwResultStatus fsVerify(const void* /*partition*/, uint32_t expectedVersion,
                            const uint8_t* offerDigest) {
  // The receiver already wrote the spiffs partition raw; mount it read-only
  // and verify the LOGICAL contents (raw bytes carry per-lamp metadata).
  if (!SPIFFS.begin(/*formatOnFail=*/false)) {
    return lp::FwResultStatus::FsMountFail;
  }
  std::vector<lamp::firmware::FsManifestFile> files;
  std::vector<uint8_t> lsig;
  if (!enumerateManifest(files, lsig) ||
      lsig.size() != lamp::firmware::kFsSigLen) {
    return lp::FwResultStatus::FsDigestMismatch;
  }
  // Bind the written image to the offer-time verified digest before trusting the
  // embedded signature, so a source can't pass a verified offer then write
  // different (also-signed) content. Same streamed-vs-offered digest check as
  // the firmware path.
  uint8_t manifestDigest[32];
  if (!lamp::firmware::computeFsManifestDigest(files, manifestDigest)) {
    return lp::FwResultStatus::FsDigestMismatch;
  }
  if (offerDigest && std::memcmp(manifestDigest, offerDigest,
                                 lp::FW_SHA256_FULL_LEN) != 0) {
    return lp::FwResultStatus::OfferShaMismatch;
  }
  uint32_t footerVersion = 0;
  if (!lamp::firmware::verifyFsManifest(files, lsig.data(), lsig.size(),
                                        &footerVersion)) {
    return lp::FwResultStatus::SignatureFail;
  }
  if (footerVersion != expectedVersion) {
    return lp::FwResultStatus::VersionMismatch;
  }
  return lp::FwResultStatus::Success;
}

// Post-verify, no reboot: drop the verify mount and remount cleanly so the new
// files are live for the webapp, then refresh the local digest so HELLO
// advertises the new fingerprint (peers observe convergence).
void fsFinalize() {
  SPIFFS.end();
  computeLocalDigest();  // remounts (begin false) + recomputes digest/version
#ifdef LAMP_DEBUG
  Serial.printf("[fs_ota] applied (no reboot); digestReady=%d version=0x%08lx\n",
                (int)s_localDigestReady, (unsigned long)s_localFsVersion);
#endif
}

// Read-only source; SPIFFS files don't change at runtime so no unmount needed.
const void* fsDistPartition() { return static_cast<const void*>(s_spiffsPart); }

bool fsLengthAndDigest(uint32_t* outLen, uint8_t outPrefix[8],
                       uint8_t outFullDigest[32], uint8_t outSignature[64]) {
  if (!s_spiffsPart || !s_localDigestReady || !s_localSigReady) return false;
  *outLen = s_spiffsPart->size;  // mkspiffs pads the image to the partition size
  std::memcpy(outPrefix, s_localDigest, lp::FW_SHA256_PREFIX_LEN);
  std::memcpy(outFullDigest, s_localDigest, lp::FW_SHA256_FULL_LEN);
  std::memcpy(outSignature, s_localSignature, lp::FW_SIG_LEN);
  return true;
}

// For the FS receiver: is the firmware OTA path busy?
bool firmwarePathBusy() {
  return (s_fwRecv && s_fwRecv->isInProgress()) ||
         (s_fwDist && s_fwDist->isInProgress());
}

const lamp::FsReceiverHooks kRecvHooks = {
    /*partition=*/   &fsRecvPartition,
    /*shouldAccept=*/&fsShouldAccept,
    /*verify=*/      &fsVerify,
    /*finalize=*/    &fsFinalize,
    /*acceptType=*/  lp::MSG_FS_ACCEPT,
    /*reqType=*/     lp::MSG_FS_REQ,
    /*resultType=*/  lp::MSG_FS_RESULT,
};
const lamp::FsDistributorHooks kDistHooks = {
    /*partition=*/      &fsDistPartition,
    /*lengthAndDigest=*/&fsLengthAndDigest,
    /*offerType=*/      lp::MSG_FS_OFFER,
    /*chunkType=*/      lp::MSG_FS_CHUNK,
    /*doneType=*/       lp::MSG_FS_DONE,
};

}  // namespace

bool fsPathBusy() {
  return s_fsReceiver.isInProgress() || s_fsDistributor.isInProgress();
}

void begin(lamp::FirmwareTransport* meshTransport,
           lamp::FirmwareReceiver* fwReceiver,
           lamp::FirmwareDistributor* fwDistributor) {
  s_fwRecv = fwReceiver;
  s_fwDist = fwDistributor;

  s_spiffsPart = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  if (!s_spiffsPart) {
#ifdef LAMP_DEBUG
    Serial.println("[fs_ota] no spiffs partition — disabled");
#endif
    return;
  }

  // Compute the local image digest BEFORE the distributor begins (its hook
  // reads it to advertise the OFFER fingerprint).
  computeLocalDigest();

  s_fsReceiver.setFsHooks(&kRecvHooks);
  s_fsReceiver.setBusyGuard(&firmwarePathBusy);
  s_fsReceiver.begin(meshTransport);

  s_fsDistributor.setFsHooks(&kDistHooks);
  s_fsDistributor.begin(meshTransport);

  // The firmware receiver must also decline while an FS OTA is mid-flow.
  if (fwReceiver) fwReceiver->setBusyGuard(&fsPathBusy);

#ifdef LAMP_DEBUG
  Serial.printf("[fs_ota] online; spiffs=%uB digestReady=%d\n",
                (unsigned)s_spiffsPart->size, (int)s_localDigestReady);
#endif
}

void tick(uint32_t nowMs) {
  s_fsReceiver.tick(nowMs);
  s_fsDistributor.tick(nowMs);
}

const uint8_t* localDigestPrefix() {
  return s_localDigestReady ? s_localDigest : nullptr;
}

bool needsFs() {
  // FS-capable (partition present) but no valid local digest → SPIFFS
  // unmountable / empty. The offer a peer sends is version-coupled to its own
  // running firmware, and this lamp runs that same version, so no extra gate is
  // needed. A digest-having lamp advertises via FS_STATE instead.
  return s_spiffsPart && !s_localDigestReady;
}

void considerPeer(const uint8_t peerMac[6], uint32_t peerFwVersion,
                  uint8_t peerProtocolVersion, uint32_t nowMs,
                  const char* peerFwChannel, const uint8_t* peerFsDigest,
                  bool peerHasFsDigest, bool peerNeedsFs, int8_t peerRssi) {
  // A peer with no digest and no need-FS flag is legacy / FS-disabled → skip.
  // A need-FS peer (empty/unmountable FS) has no digest to compare, so it takes
  // the offer unconditionally below.
  if ((!peerHasFsDigest && !peerNeedsFs) || !s_localDigestReady) return;
  // Self-version gate: only distribute an FS image that matches the running
  // firmware. A lamp firmware-OTA'd to vN whose SPIFFS is still the stale vN-1
  // image must not push it (the OFFER advertises the firmware version, so a
  // peer can't tell the content is stale until verify, by which point it has
  // already overwritten its good image). Keeps FS distribution directional.
  if (s_localFsVersion != lamp::FIRMWARE_VERSION) return;
  // Version coupling: the peer must be running this firmware version.
  if (peerFwVersion != lamp::FIRMWARE_VERSION) return;
  // A need-FS peer has no digest to compare; it always wants the image. A peer
  // with a digest that already matches ours needs nothing.
  if (peerHasFsDigest &&
      std::memcmp(peerFsDigest, s_localDigest, lp::FW_SHA256_PREFIX_LEN) == 0) {
    return;
  }
  // Don't distribute while any OTA (FS or firmware) is mid-flow, or while the
  // local web window is open.
  if (firmwarePathBusy() || s_fsReceiver.isInProgress() || webapp::isActive()) {
    return;
  }
  s_fsDistributor.considerPeerForOta(peerMac, peerFwVersion,
                                     peerProtocolVersion, nowMs, peerFwChannel,
                                     /*peerMaxChunk=*/0, peerRssi);
}

void handleControl(const lamp::PendingFirmwareControl& ctrl) {
  // The FS receiver reuses handleControlOnLoop, which discriminates on
  // MSG_FW_OFFER / MSG_FW_DONE. Map the FS msgType to its firmware twin; the
  // outbound responses still carry MSG_FS_* via the receiver's fsHooks_.
  lamp::PendingFirmwareControl mapped = ctrl;
  if (ctrl.msgType == lp::MSG_FS_OFFER) {
    mapped.msgType = lp::MSG_FW_OFFER;
  } else if (ctrl.msgType == lp::MSG_FS_DONE) {
    mapped.msgType = lp::MSG_FW_DONE;
  } else {
    return;
  }
  s_fsReceiver.handleControlOnLoop(mapped);
}

void onChunk(const lp::ParsedFwChunk& c)  { s_fsReceiver.handleChunkOnRecvTask(c); }
void onAccept(const lp::ParsedFwAccept& a) { s_fsDistributor.onAcceptOnRecvTask(a); }
void onReq(const lp::ParsedFwReq& r)       { s_fsDistributor.onReqOnRecvTask(r); }
void onResult(const lp::ParsedFwResult& r) { s_fsDistributor.onResultOnRecvTask(r); }

}  // namespace fs_ota

#else

namespace fs_ota {
// No OTA path runs on the native build; the firmware start gate still links this.
bool fsPathBusy() { return false; }
}  // namespace fs_ota

#endif  // ARDUINO || ESP_PLATFORM
