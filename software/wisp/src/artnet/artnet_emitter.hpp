// ArtnetEmitter broadcasts ArtNet DMX universe-1 frames over UDP/6454
// so pre-mesh lamps get the same palette the mesh lamps receive.
// Emits whenever WifiLink::canBroadcast() (STA associated or softAP up);
// a cheap no-op otherwise. Pure frame construction lives in artnet_frame.{h,cpp}.

#pragma once

#include <AsyncUDP.h>

#include <cstdint>

namespace wisp {

class CurrentPalette;
class WifiLink;

class ArtnetEmitter {
 public:
  void begin(CurrentPalette* palette, WifiLink* wifi);

  // Called on every palette change (Aurora or manual). Kicks an immediate
  // emit if WiFi is connected.
  void onPaletteChanged();

  // Periodic tick from loop(). Pushes a backstop refresh frame every
  // kBackstopMs so a lamp that booted between palette changes still picks
  // up the current colors.
  void tick(uint32_t nowMs);

  // Gates emission. Off (source Off, or Aurora with no live stream) sets
  // this false so old lamps keep their last frame instead of being driven
  // to the off-color over ArtNet; mesh lamps get the equivalent release
  // via isWispActive=0.
  void setActive(bool active);

 private:
  void emitNow();

  static constexpr uint16_t kArtnetPort = 6454;
  // Old lamps are low priority and tolerate lag, so feed them at ~1 Hz. A
  // faster stream steals TX airtime from ESP-NOW mesh RX on the shared radio.
  static constexpr uint32_t kBackstopMs = 1000;
  static constexpr size_t kMaxStageLamps = 16;

  CurrentPalette* palette_ = nullptr;
  WifiLink* wifi_ = nullptr;
  AsyncUDP udp_;
  bool udpReady_ = false;
  bool active_ = true;
  uint8_t seq_ = 0;
  uint32_t lastEmitMs_ = 0;
#ifdef LAMP_DEBUG
  size_t lastClientCount_ = SIZE_MAX;
#endif
};

}  // namespace wisp
