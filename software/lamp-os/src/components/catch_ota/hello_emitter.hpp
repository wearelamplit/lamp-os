#pragma once

#include <cstddef>
#include <cstdint>

namespace catch_ota {

constexpr uint32_t kHelloIntervalMs = 5000;

// Set the identity advertised in HELLOs — the lamp's real name + base/shade
// RGBW — once at startup so it shows up correctly on the mesh while waiting to
// be upgraded. Until set, colors are zero and the name empty.
void helloSetIdentity(const char* name, const uint8_t baseRGBW[4],
                      const uint8_t shadeRGBW[4]);

// Builds a v0x05 HELLO frame into buf, using the colors set via
// helloSetIdentity. buf must be at least HELLO_MAX_SIZE bytes. Returns frame
// length on success, 0 on error.
size_t composeHello(uint8_t* buf, const uint8_t mac[6], const char* name);

// Broadcasts a HELLO every kHelloIntervalMs. Suppressed while isInProgress().
// Must be called from the loop task on every iteration.
void helloTick(uint32_t nowMs);

}  // namespace catch_ota
