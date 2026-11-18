// OscListener feeds OSC packets from USB serial (SLIP frames, handed in by
// SerialConsole) and UDP into WispController's OSC palette input, reading
// the address of the stored target (WispConfig::oscTarget). Loop task only.
// The UDP socket is bound on every interface (softAP and STA) while
// `poll(true)` and closed on `poll(false)`.

#pragma once

#include <cstddef>
#include <cstdint>

#include "paint/current_palette.hpp"  // PaletteColor
#include "util/rate_gate.hpp"

namespace wisp {

class WispController;
class WispConfig;

inline constexpr uint16_t kOscUdpPort = 9000;
// The most colours one OSC palette carries; a longer one is refused whole.
// Its own limit, wider than the Manual palette's.
inline constexpr size_t kOscPaletteMaxColors = 50;
inline constexpr char kStraylightAddressPrefix[] = "/straylight/";

class OscListener {
 public:
  OscListener(WispController& controller, const WispConfig& config)
      : controller_(controller), config_(config) {}

  // Call every loop pass, and at least once per lampos::kRetiredAgeMs.
  void poll(bool active, uint32_t nowMs);
  void handlePacket(const uint8_t* data, size_t len, const char* via);

 private:
  void open(uint32_t nowMs);
  void close();
  void handleStraylight(const uint8_t* data, size_t len, const char* via);

  WispController& controller_;
  const WispConfig& config_;
  int fd_ = -1;
  uint32_t lastOpenAttemptMs_ = 0;
  bool attempted_ = false;
  RateGate refusedLog_{1000};
  uint8_t rx_[1500];
  // Held off the 8 KB loop stack, since the palette path under handlePacket
  // runs deep.
  float rgb_[kOscPaletteMaxColors * 3];
  PaletteColor colours_[kOscPaletteMaxColors];
};

}  // namespace wisp
