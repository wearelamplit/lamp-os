#include "artnet/artnet_emitter.hpp"

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>

#include "paint/current_palette.hpp"
#include "net/wifi_link.hpp"
#include "artnet/artnet_frame.hpp"

namespace wisp {

void ArtnetEmitter::begin(CurrentPalette* palette, WifiLink* wifi) {
  palette_ = palette;
  wifi_ = wifi;
  lastEmitMs_ = 0;  // force first tick to emit promptly once WiFi is up
}

void ArtnetEmitter::onPaletteChanged() {
  emitNow();
}

void ArtnetEmitter::tick(uint32_t nowMs) {
  if (nowMs - lastEmitMs_ < kBackstopMs) return;
  emitNow();
}

void ArtnetEmitter::emitNow() {
  if (!palette_ || !wifi_) return;
  if (!wifi_->canBroadcast()) {
    // Stamp lastEmitMs_ so tick() backs off to backstop cadence instead of
    // re-entering emitNow() on every loop iteration while the link comes up.
    lastEmitMs_ = millis();
    return;
  }

  if (!udpReady_) {
    // AsyncUDP::connect for outgoing only; no listen needed.
    // Uses writeTo() with broadcast addr per call.
    udpReady_ = true;
    Serial.println("[artnet] udp ready");
  }

  uint8_t buf[kArtnetFrameSize];
  size_t n = buildArtnetDmxFrame(*palette_, seq_++, buf, sizeof(buf));
  if (n != kArtnetFrameSize) {
    Serial.println("[artnet] frame build failed");
    return;
  }

  lastEmitMs_ = millis();

  // WiFi broadcast has no MAC-layer ack/retry and is dropped by lamps; unicast
  // to each joined station instead. STA mode keeps the directed broadcast.
  if (wifi_->isAp()) {
    IPAddress clients[kMaxStageLamps];
    const size_t count = wifi_->apClientIps(clients, kMaxStageLamps);
#ifdef LAMP_DEBUG
    if (count != lastClientCount_) {
      lastClientCount_ = count;
      if (count == 0) {
        Serial.println("[artnet] AP up, 0 stations joined; nothing to serve");
      } else {
        Serial.printf("[artnet] serving %u station(s), first=%s\n",
                      (unsigned)count, clients[0].toString().c_str());
      }
    }
#endif
    for (size_t i = 0; i < count; ++i) {
      udp_.writeTo(buf, n, clients[i], kArtnetPort, TCPIP_ADAPTER_IF_AP);
    }
    return;
  }

  size_t sent = udp_.writeTo(buf, n, IPAddress(255, 255, 255, 255), kArtnetPort,
                             TCPIP_ADAPTER_IF_STA);
  if (sent != n) {
    Serial.printf("[artnet] short write %u/%u\n", (unsigned)sent, (unsigned)n);
  }
}

}  // namespace wisp
