#include "osc/osc_listener.hpp"

#include <Arduino.h>
#include <lwip/sockets.h>

#include <lampos/clock.hpp>

#include "config/wisp_config.hpp"
#include "core/wisp_controller.hpp"
#include "osc/osc_message.hpp"
#include "paint/current_palette.hpp"

namespace wisp {

namespace {
constexpr uint32_t kOpenRetryMs = 5000;
// lwIP queues at most 6 datagrams per UDP socket (UDP_RECVMBOX_SIZE), so 8
// drains a full mailbox in one pass and still bounds loop-task time when a
// sender floods the port.
constexpr int kMaxPacketsPerPoll = 8;
}  // namespace

void OscListener::open(uint32_t nowMs) {
  if (attempted_ && nowMs - lastOpenAttemptMs_ < kOpenRetryMs) return;
  attempted_ = true;
  lastOpenAttemptMs_ = nowMs;
  const int fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    Serial.println("[osc] udp socket failed");
    return;
  }
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kOscUdpPort);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (lwip_bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    Serial.printf("[osc] udp bind :%u failed\n", (unsigned)kOscUdpPort);
    lwip_close(fd);
    return;
  }
  fd_ = fd;
  Serial.printf("[osc] udp listening on :%u\n", (unsigned)kOscUdpPort);
}

void OscListener::close() {
  if (fd_ < 0) return;
  lwip_close(fd_);
  fd_ = -1;
  attempted_ = false;
  Serial.println("[osc] udp closed");
}

void OscListener::poll(bool active, uint32_t nowMs) {
  lastOpenAttemptMs_ = lampos::retireStamp(nowMs, lastOpenAttemptMs_);
  if (!active) {
    close();
    return;
  }
  if (fd_ < 0) open(nowMs);
  if (fd_ < 0) return;
  for (int i = 0; i < kMaxPacketsPerPoll; ++i) {
    const int n = lwip_recv(fd_, rx_, sizeof(rx_), MSG_DONTWAIT);
    if (n <= 0) break;
    handlePacket(rx_, static_cast<size_t>(n), "udp");
  }
}

void OscListener::handlePacket(const uint8_t* data, size_t len,
                               const char* via) {
  const OscTarget target = config_.oscTarget();
  if (target.mode == OscMode::Straylight) {
    handleStraylight(data, len, via);
    return;
  }
  char address[kOscTargetAddressCap];
  oscTargetAddress(target, address, sizeof(address));
  size_t count = 0;
  const char* refusedTags = nullptr;
  switch (oscFindColours(data, len, address, colours_, kOscPaletteMaxColors,
                         count, refusedTags)) {
    case OscColours::Read:
      controller_.onOscPalette(colours_, count, via, millis());
      return;
    case OscColours::Refused:
      if (!refusedLog_.request(millis())) return;
      Serial.printf("[osc] refused %.32s ,%.32s via %s\n", address,
                    refusedTags ? refusedTags : "", via);
      return;
    case OscColours::Absent:
      return;
  }
}

void OscListener::handleStraylight(const uint8_t* data, size_t len,
                                   const char* via) {
  size_t n = 0;
  switch (oscClassifyFeedPacket(data, len, oscPaletteAddress(config_.oscRole()),
                                kStraylightAddressPrefix, rgb_,
                                kOscPaletteMaxColors * 3, n)) {
    case OscFeedPacket::Palette:
      break;
    case OscFeedPacket::Feed:
      controller_.onOscFeed(millis());
      return;
    case OscFeedPacket::Other:
      return;
  }
  if (n == 0 || n % 3 != 0) return;
  const size_t count = n / 3;
  for (size_t i = 0; i < count; ++i) {
    colours_[i].r = rgb_[i * 3 + 0];
    colours_[i].g = rgb_[i * 3 + 1];
    colours_[i].b = rgb_[i * 3 + 2];
    colours_[i].w = 0.0f;
  }
  controller_.onOscPalette(colours_, count, via, millis());
}

}  // namespace wisp
