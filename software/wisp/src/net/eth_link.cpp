#include "net/eth_link.hpp"

#include <Arduino.h>
#include <ETH.h>
#include <driver/gpio.h>
#include <driver/spi_common.h>

#include <cstdio>

#include "net/hostname.hpp"

namespace wisp::eth {

namespace {

constexpr spi_host_device_t kSpiHost = SPI2_HOST;
constexpr int kPhyAddr = 1;
constexpr int kNoResetPin = -1;
constexpr size_t kHostnameCap = 32;
constexpr int kPins[] = {WISP_ETH_SCK, WISP_ETH_MISO, WISP_ETH_MOSI,
                         WISP_ETH_CS, WISP_ETH_INT};

bool s_present = false;

void logEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.printf("[eth] link up %u Mbps %s duplex\n",
                    (unsigned)ETH.linkSpeed(),
                    ETH.fullDuplex() ? "full" : "half");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("[eth] link down");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP: {
      char ip[16];
      formatAddr(IPAddress(info.got_ip.ip_info.ip.addr), ip, sizeof(ip));
      Serial.printf("[eth] got IP %s\n", ip);
      break;
    }
    case ARDUINO_EVENT_ETH_LOST_IP:
      Serial.println("[eth] lost IP");
      break;
    default:
      break;
  }
}

}  // namespace

void begin(const char* name) {
  if (s_present) return;
  s_present = ETH.begin(ETH_PHY_W5500, kPhyAddr, WISP_ETH_CS, WISP_ETH_INT,
                        kNoResetPin, kSpiHost, WISP_ETH_SCK, WISP_ETH_MISO,
                        WISP_ETH_MOSI);
  if (!s_present) {
    // A failed probe leaves the bus initialized with SCK and MOSI driven.
    const esp_err_t freed = spi_bus_free(kSpiHost);
    if (freed != ESP_OK) {
      Serial.printf("[eth] spi_bus_free failed: %d\n", (int)freed);
    }
    for (const int pin : kPins) gpio_reset_pin(static_cast<gpio_num_t>(pin));
    Serial.printf("[eth] no W5500 (cs=%d); Ethernet off\n", WISP_ETH_CS);
    return;
  }
  // The core turns all-multicast on; a lighting LAN's sACN would then cost an
  // SPI read and a heap buffer per frame for packets nothing here wants.
  ETH.receiveAllMulticast(false);
  // DHCP starts on link-up, after this, so the lease request carries it.
  char hostname[kHostnameCap];
  toHostname(name, "wisp", hostname, sizeof(hostname));
  ETH.setHostname(hostname);
  Network.onEvent(logEvent);
  Serial.printf("[eth] W5500 up as %s; waiting for link\n", hostname);
}

bool up() { return s_present && ETH.connected() && ETH.hasIP(); }

void formatAddr(const IPAddress& ip, char* out, size_t cap) {
  std::snprintf(out, cap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void formatIp(char* out, size_t cap) {
  if (cap > 0) out[0] = '\0';
  if (!up()) return;
  const IPAddress ip = ETH.localIP();
  if (ip == IPAddress()) return;
  formatAddr(ip, out, cap);
}

void printStatus(Print& out) {
  if (!s_present) {
    out.println("[eth] no W5500");
    return;
  }
  char ip[16];
  formatIp(ip, sizeof(ip));
  if (!ETH.connected()) {
    out.println("[eth] link down");
  } else {
    out.printf("[eth] link up %u Mbps %s duplex ip=%s\n",
               (unsigned)ETH.linkSpeed(), ETH.fullDuplex() ? "full" : "half",
               ip[0] ? ip : "(none)");
  }
}

}  // namespace wisp::eth
