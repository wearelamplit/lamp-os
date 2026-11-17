// Wired Ethernet over a WIZnet W5500 on SPI, carrying the OSC feed so the
// radio stays on ESP-NOW. Pins are the WISP_ETH_* build flags in
// platformio.ini; the wiring table is in the wisp README. Loop task only,
// except up() and the getters, which read netif status bits and are safe from
// any task. Ethernet never touches the radio.

#pragma once

#include <IPAddress.h>
#include <Print.h>

namespace wisp::eth {

// Probes for a W5500 once and, when one answers, starts it with DHCP under
// `name` made a valid hostname (read at boot only). With none fitted the probe
// fails in at most ~200 ms and resets every pin, so nothing is driven
// afterwards and nothing retries.
void begin(const char* name);

// Link up and a DHCP lease held: the OSC feed can arrive over the cable.
bool up();

// The leased address as dotted text into out (at least 16 B), empty while
// !up().
void formatIp(char* out, size_t cap);

// `ip` as dotted text into out (at least 16 B).
void formatAddr(const IPAddress& ip, char* out, size_t cap);

// eth:show console line.
void printStatus(Print& out);

}  // namespace wisp::eth
