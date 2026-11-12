// OscListener feeds OSC packets from USB serial (SLIP frames, handed in by
// SerialConsole) and UDP into WispController's OSC palette input, reading
// the address for the configured role. Loop task only. The UDP socket is
// bound on every interface (softAP and STA) while `poll(true)` and closed on
// `poll(false)`.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

class WispController;
class WispConfig;

inline constexpr uint16_t kOscUdpPort = 9000;
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

  WispController& controller_;
  const WispConfig& config_;
  int fd_ = -1;
  uint32_t lastOpenAttemptMs_ = 0;
  bool attempted_ = false;
  uint8_t rx_[1500];
};

}  // namespace wisp
