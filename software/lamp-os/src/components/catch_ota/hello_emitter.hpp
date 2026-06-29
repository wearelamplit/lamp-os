#pragma once

#include <cstddef>
#include <cstdint>

namespace catch_ota {

constexpr uint32_t kHelloIntervalMs = 5000;

// Builds a v0x05 HELLO frame into buf. name is the caller-supplied device
// name (e.g. "m-aabbcc"). buf must be at least HELLO_MAX_SIZE bytes.
// Returns frame length on success, 0 on error.
size_t composeHello(uint8_t* buf, const uint8_t mac[6], const char* name);

// Broadcasts a HELLO every kHelloIntervalMs. Suppressed while isInProgress().
// Must be called from the loop task on every iteration.
void helloTick(uint32_t nowMs);

}  // namespace catch_ota
