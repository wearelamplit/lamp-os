#pragma once

namespace lamp {

// One `[drop]` serial line per dropped live-control write, rate-limited
// across every caller. Callable from any task. Compiles to nothing outside
// LAMP_DEBUG.
#ifdef LAMP_DEBUG
void logDroppedWrite(const char* characteristic, const char* reason);
#else
inline void logDroppedWrite(const char*, const char*) {}
#endif

}  // namespace lamp
