#pragma once

// FS-image OTA — delivers the SPIFFS web-UI image over the mesh to the spiffs
// partition, reusing the firmware OTA engine via FsReceiverHooks /
// FsDistributorHooks.
//
// It owns a second FirmwareReceiver + FirmwareDistributor wired to the spiffs
// partition + the FS verify (mount + manifest digest + ed25519 against
// fw.lsig). Inbound MSG_FS_* frames are routed here by mesh_link; outbound
// MSG_FS_* are emitted by the FS instances. A cross-OTA busy guard keeps the
// FS and firmware paths from running concurrently (shared erase machinery).

#include <cstddef>
#include <cstdint>

namespace lamp {
class FirmwareTransport;
class FirmwareReceiver;
class FirmwareDistributor;
struct PendingFirmwareControl;
}  // namespace lamp

namespace lamp_protocol {
struct ParsedFwChunk;
struct ParsedFwAccept;
struct ParsedFwReq;
struct ParsedFwResult;
}  // namespace lamp_protocol

namespace fs_ota {

// Resolve the spiffs partition, compute the local manifest digest, wire the FS
// receiver/distributor hooks + cross-OTA guards, and begin them. Call from
// Lamp::setup AFTER firmwareReceiver/firmwareDistributor.begin() (so the busy
// guards can reference them) and AFTER SPIFFS is mountable.
void begin(lamp::FirmwareTransport* meshTransport,
           lamp::FirmwareReceiver* fwReceiver,
           lamp::FirmwareDistributor* fwDistributor);

// Core 1 tick — drives the FS receiver + distributor state machines.
void tick(uint32_t nowMs);

// 8-byte prefix of our local FS manifest digest for HELLO_TLV_FS_STATE, or
// nullptr if not yet computed (SPIFFS unmountable / empty).
const uint8_t* localDigestPrefix();

// Offer our FS image to a peer if it needs it: peer at our firmware version,
// advertising an FS digest that differs from ours. Called from the social
// peer loop (Core 1). hasFsDigest=false (older / FS-disabled peer) → skip.
void considerPeer(const uint8_t peerMac[6], uint32_t peerFwVersion,
                  uint8_t peerProtocolVersion, uint32_t nowMs,
                  const char* peerFwChannel, const uint8_t* peerFsDigest,
                  bool peerHasFsDigest);

// --- Inbound dispatch (called from mesh_link's WiFi recv task) -------------
// MSG_FS_OFFER / MSG_FS_DONE — posted to the Core 1 drain (like firmware).
// handleControl is the Core 1 side (called by the lamp.cpp drain).
void handleControl(const lamp::PendingFirmwareControl& ctrl);
// MSG_FS_CHUNK — written straight to the spiffs partition on Core 0.
void onChunk(const lamp_protocol::ParsedFwChunk& c);
// MSG_FS_ACCEPT / REQ / RESULT — to the FS distributor (we're the sender).
void onAccept(const lamp_protocol::ParsedFwAccept& a);
void onReq(const lamp_protocol::ParsedFwReq& r);
void onResult(const lamp_protocol::ParsedFwResult& r);

}  // namespace fs_ota
