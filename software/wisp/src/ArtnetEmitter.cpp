#include "ArtnetEmitter.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>

#include "CurrentPalette.h"
#include "WifiLink.h"
#include "artnet_frame.h"

namespace wisp {

void ArtnetEmitter::begin(CurrentPalette* palette, WifiLink* wifi) {
  palette_ = palette;
  wifi_ = wifi;
  // UDP socket is opened lazily on first emit; until then we don't know
  // if WiFi is connected.
}

void ArtnetEmitter::setEnabled(bool on) {
  enabled_ = on;
  if (on) {
    lastEmitMs_ = 0;  // force first emit on next tick
    Serial.println("[artnet] enabled");
  } else {
    Serial.println("[artnet] disabled");
  }
}

void ArtnetEmitter::onPaletteChanged() {
  if (!enabled_) return;
  emitNow();
}

void ArtnetEmitter::tick(uint32_t nowMs) {
  if (!enabled_) return;
  if (nowMs - lastEmitMs_ < kBackstopMs) return;
  emitNow();
}

void ArtnetEmitter::emitNow() {
  if (!palette_ || !wifi_) return;
  if (!wifi_->isConnected()) {
    // Stamp lastEmitMs_ so tick() backs off to backstop cadence instead of
    // re-entering emitNow() on every loop iteration while we wait for STA.
    lastEmitMs_ = millis();
    return;
  }

  if (!udpReady_) {
    // AsyncUDP::connect for outgoing only; no listen needed.
    // We use writeTo() with broadcast addr per call.
    udpReady_ = true;
    Serial.println("[artnet] udp ready");
  }

  uint8_t buf[kArtnetFrameSize];
  size_t n = buildArtnetDmxFrame(*palette_, seq_++, buf, sizeof(buf));
  if (n != kArtnetFrameSize) {
    Serial.println("[artnet] frame build failed");
    return;
  }

  IPAddress bcast(255, 255, 255, 255);
  size_t sent = udp_.writeTo(buf, n, bcast, kArtnetPort);
  lastEmitMs_ = millis();
  if (sent != n) {
    Serial.printf("[artnet] short write %u/%u\n",
                  (unsigned)sent, (unsigned)n);
  }
}

}  // namespace wisp
