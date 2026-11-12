#include "hello_relay_suppressor.hpp"

#include <cstring>

#include <lampos/clock.hpp>

#include "relay_delay.hpp"

namespace lamp {

void HelloRelaySuppressor::begin(const uint8_t ownMac[6]) {
  LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
  std::memcpy(ownMac_, ownMac, 6);
  LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
}

uint32_t HelloRelaySuppressor::jitterMs(const uint8_t mac[6], uint16_t seq) const {
  return relayDelayMs(ownMac_, mac, lamp_protocol::MSG_HELLO, seq,
                      kHelloRelayJitterMinMs, kHelloRelayJitterMaxMs);
}

HelloRelaySuppressor::Pending* HelloRelaySuppressor::find(const uint8_t mac[6],
                                                          uint16_t seq) {
  for (auto& e : slots_) {
    if (e.used && e.seq == seq && std::memcmp(e.mac, mac, 6) == 0) return &e;
  }
  return nullptr;
}

bool HelloRelaySuppressor::onFirstSeen(const uint8_t mac[6], uint16_t seq,
                                       const uint8_t* frame, size_t len,
                                       uint32_t nowMs) {
  if (len > lamp_protocol::HELLO_MAX_SIZE) {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
#ifdef LAMP_DEBUG
    relayed_++;
#endif
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    return true;
  }
  // The caller's helloDedup_ ring guarantees one first-seen per (mac, seq), so
  // a free slot is claimed without deduping against pending entries.
  LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
  Pending* slot = nullptr;
  for (auto& e : slots_) {
    if (!e.used) { slot = &e; break; }
  }
  if (!slot) {
#ifdef LAMP_DEBUG
    relayed_++;
#endif
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    return true;
  }

  slot->used = true;
  std::memcpy(slot->mac, mac, 6);
  slot->seq = seq;
  slot->dupCount = 0;
  slot->fireAtMs = nowMs + jitterMs(mac, seq);
  slot->len = static_cast<uint16_t>(len);
  std::memcpy(slot->frame, frame, len);
  LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
  return false;
}

void HelloRelaySuppressor::onDuplicate(const uint8_t mac[6], uint16_t seq) {
  LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
  if (Pending* e = find(mac, seq); e && e->dupCount < 0xFF) e->dupCount++;
  LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
}

void HelloRelaySuppressor::tick(uint32_t nowMs, const RelayFn& relay) {
  // Copy one due frame out under the lock, release, then relay. The relay
  // broadcast must never run inside the critical section.
  for (auto& e : slots_) {
    uint8_t frame[lamp_protocol::HELLO_MAX_SIZE];
    uint16_t len = 0;
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    if (e.used && lampos::timeReached(nowMs, e.fireAtMs)) {
      if (e.dupCount < kHelloSuppressThreshold) {
        std::memcpy(frame, e.frame, e.len);
        len = e.len;
#ifdef LAMP_DEBUG
        relayed_++;
#endif
      } else {
#ifdef LAMP_DEBUG
        suppressed_++;
#endif
      }
      e.used = false;
    }
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    if (len) relay(frame, len);
  }
}

}  // namespace lamp
