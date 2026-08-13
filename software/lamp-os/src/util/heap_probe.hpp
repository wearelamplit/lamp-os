#pragma once

#ifdef LAMP_DEBUG
#include <Arduino.h>

#include "esp_heap_caps.h"
#include "esp_system.h"
#endif

namespace lamp {

// free = total free heap; largest = biggest contiguous block. A wide gap
// (largest << free) is the fragmentation signal. The heap has room but not
// in one piece.
#ifdef LAMP_DEBUG
inline void logHeap(const char* at) {
  Serial.printf("[heap] at=%s free=%u largest=%u\n", at,
                (unsigned)esp_get_free_heap_size(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
#else
inline void logHeap(const char* at) { (void)at; }
#endif

}  // namespace lamp
