// ArtnetEmitter — broadcasts ArtNet DMX universe-1 frames over UDP/6454
// so pre-mesh lamps running software/lamp-os firmware can be painted by
// the same Aurora palette that drives the v0x03 mesh.
//
// Mirrors PaintDistributor's lifecycle: enabled/disabled gate + palette
// change kick + low-rate backstop refresh. Pure frame construction lives
// in artnet_frame.{h,cpp}; this class just owns the AsyncUDP socket and
// decides when to emit.

#pragma once

#include <AsyncUDP.h>

#include <cstdint>

namespace wisp {

class CurrentPalette;
class WifiLink;

class ArtnetEmitter {
 public:
  void begin(CurrentPalette* palette, WifiLink* wifi);

  // External enable/disable — wired to a serial command and (later) the
  // app pane. Defaults to disabled so the wisp doesn't shout into a
  // venue's LAN unsolicited.
  void setEnabled(bool on);
  bool enabled() const { return enabled_; }

  // Called from the Aurora palette callback in main.cpp. Kicks an
  // immediate emit if enabled + connected.
  void onPaletteChanged();

  // Periodic tick from loop(). Pushes a backstop refresh frame every
  // kBackstopMs while enabled, so a lamp that booted between palette
  // changes still picks up the current colors.
  void tick(uint32_t nowMs);

 private:
  void emitNow();

  static constexpr uint16_t kArtnetPort = 6454;
  static constexpr uint32_t kBackstopMs = 1000;

  CurrentPalette* palette_ = nullptr;
  WifiLink* wifi_ = nullptr;
  AsyncUDP udp_;
  bool udpReady_ = false;
  bool enabled_ = false;
  uint8_t seq_ = 0;
  uint32_t lastEmitMs_ = 0;
};

}  // namespace wisp
