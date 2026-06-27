#if LAMP_FS_OTA_ENABLED

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
#include "components/network/lamp_protocol.hpp"
#include "components/webapp/webapp.hpp"
#include "version.hpp"

namespace fs_ota {
namespace {

namespace lp = lamp_protocol;

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

// --- SPIFFS manifest enumeration ------------------------------------------

// SPIFFS must be mounted. Fills `files` (excluding fw.lsig) with whole-content
// readers backed by `contents`, and `lsig` with the fw.lsig bytes. Returns
// true iff fw.lsig was present. The `contents` vectors must outlive any use of
// `files` (the readers point into them).
bool enumerateManifest(std::vector<std::vector<uint8_t>>& contents,
                       std::vector<lamp::firmware::FsManifestFile>& files,
                       std::vector<uint8_t>& lsig) {
  File root = SPIFFS.open("/");
  if (!root) return false;
  // Reserve so emplace_back never reallocates (which would dangle the readers'
  // captured data pointers).
  contents.reserve(16);
  bool haveLsig = false;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (f.isDirectory()) continue;
    // Canonical name: strip any leading '/' so it matches sign_fs.py's
    // basenames (the host/device agreement hinge).
    const char* raw = f.name();
    std::string name = raw ? raw : "";
    if (!name.empty() && name[0] == '/') name.erase(0, 1);
    const size_t sz = f.size();
    if (name == lamp::firmware::kFsSigName) {
      lsig.resize(sz);
      if (sz && f.read(lsig.data(), sz) != static_cast<int>(sz)) return false;
      haveLsig = true;
      continue;
    }
    contents.emplace_back();
    std::vector<uint8_t>& buf = contents.back();
    buf.resize(sz);
    if (sz && f.read(buf.data(), sz) != static_cast<int>(sz)) return false;
    lamp::firmware::FsManifestFile mf;
    mf.name = name;
    mf.contentLen = sz;
    const uint8_t* data = buf.data();
    mf.read = [data, sz](size_t off, size_t want, uint8_t* out) -> int {
      if (off + want > sz) return -1;
      std::memcpy(out, data + off, want);
      return static_cast<int>(want);
    };
    files.push_back(std::move(mf));
  }
  return haveLsig;
}

void computeLocalDigest() {
  s_localDigestReady = false;
  if (!SPIFFS.begin(/*formatOnFail=*/false)) return;
  std::vector<std::vector<uint8_t>> contents;
  std::vector<lamp::firmware::FsManifestFile> files;
  std::vector<uint8_t> lsig;
  if (!enumerateManifest(contents, files, lsig)) return;
  if (lamp::firmware::computeFsManifestDigest(files, s_localDigest)) {
    s_localDigestReady = true;
  }
}

// --- Receiver hooks --------------------------------------------------------

// Unmount SPIFFS before the receiver erases+writes the partition raw (a
// mounted SPIFFS would corrupt on raw writes), then hand it the partition.
const void* fsRecvPartition() {
  SPIFFS.end();
  return static_cast<const void*>(s_spiffsPart);
}

bool fsShouldAccept(uint32_t offerVersion, const uint8_t* offerDigestPrefix) {
  // Don't overwrite the UI partition while the web server is mid-serve.
  if (webapp::isActive()) return false;
  // Version-coupled: only an FS image stamped with our running firmware
  // version, and only if its content differs from what we already have.
  if (offerVersion != lamp::FIRMWARE_VERSION) return false;
  if (!s_localDigestReady) return true;  // no local image → take anything at our version
  return std::memcmp(offerDigestPrefix, s_localDigest,
                     lp::FW_SHA256_PREFIX_LEN) != 0;
}

lp::FwResultStatus fsVerify(const void* /*partition*/, uint32_t expectedVersion) {
  // The receiver already wrote the spiffs partition raw; mount it read-only
  // and verify the LOGICAL contents (raw bytes carry per-lamp metadata).
  if (!SPIFFS.begin(/*formatOnFail=*/false)) {
    return lp::FwResultStatus::FsMountFail;
  }
  std::vector<std::vector<uint8_t>> contents;
  std::vector<lamp::firmware::FsManifestFile> files;
  std::vector<uint8_t> lsig;
  if (!enumerateManifest(contents, files, lsig) ||
      lsig.size() != lamp::firmware::kFsSigLen) {
    return lp::FwResultStatus::FsDigestMismatch;
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

// --- Distributor hooks -----------------------------------------------------

// Read-only source; SPIFFS files don't change at runtime so no unmount needed.
const void* fsDistPartition() { return static_cast<const void*>(s_spiffsPart); }

bool fsLengthAndDigest(uint32_t* outLen, uint8_t outPrefix[8]) {
  if (!s_spiffsPart || !s_localDigestReady) return false;
  *outLen = s_spiffsPart->size;  // mkspiffs pads the image to the partition size
  std::memcpy(outPrefix, s_localDigest, lp::FW_SHA256_PREFIX_LEN);
  return true;
}

// --- Cross-OTA busy guards -------------------------------------------------

// For the FS receiver: is the firmware OTA path busy?
bool firmwarePathBusy() {
  return (s_fwRecv && s_fwRecv->isInProgress()) ||
         (s_fwDist && s_fwDist->isInProgress());
}
// For the firmware receiver: is the FS OTA path busy?
bool fsPathBusy() {
  return s_fsReceiver.isInProgress() || s_fsDistributor.isInProgress();
}

const lamp::FsReceiverHooks kRecvHooks = {
    /*partition=*/   &fsRecvPartition,
    /*shouldAccept=*/&fsShouldAccept,
    /*verify=*/      &fsVerify,
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

// ---------------------------------------------------------------------------

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

  // Compute our local image digest BEFORE the distributor begins (its hook
  // reads it to advertise the OFFER fingerprint).
  computeLocalDigest();

  s_fsReceiver.setFsHooks(&kRecvHooks);
  s_fsReceiver.setBusyGuard(&firmwarePathBusy);
  s_fsReceiver.begin(meshTransport);

  s_fsDistributor.setFsHooks(&kDistHooks);
  s_fsDistributor.begin(meshTransport);

  // The firmware receiver must also decline while WE'RE mid-FS-OTA.
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

bool isActive() {
  return s_fsReceiver.isInProgress() || s_fsDistributor.isInProgress();
}

const uint8_t* localDigestPrefix() {
  return s_localDigestReady ? s_localDigest : nullptr;
}

void considerPeer(const uint8_t peerMac[6], uint32_t peerFwVersion,
                  uint8_t peerProtocolVersion, uint32_t nowMs,
                  const char* peerFwChannel, const uint8_t* peerFsDigest,
                  bool peerHasFsDigest) {
  if (!peerHasFsDigest || !s_localDigestReady) return;
  // Version coupling: the peer must be running our firmware version.
  if (peerFwVersion != lamp::FIRMWARE_VERSION) return;
  // Peer already has our exact image → nothing to send.
  if (std::memcmp(peerFsDigest, s_localDigest, lp::FW_SHA256_PREFIX_LEN) == 0) {
    return;
  }
  // Don't distribute while any OTA (FS or firmware) is mid-flow, or while our
  // own web window is open.
  if (firmwarePathBusy() || s_fsReceiver.isInProgress() || webapp::isActive()) {
    return;
  }
  s_fsDistributor.considerPeerForOta(peerMac, peerFwVersion,
                                     peerProtocolVersion, nowMs, peerFwChannel);
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

#endif  // ARDUINO || ESP_PLATFORM
#endif  // LAMP_FS_OTA_ENABLED
