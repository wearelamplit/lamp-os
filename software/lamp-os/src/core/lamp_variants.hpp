// software/lamp-os/src/core/lamp_variants.hpp
//
// Variant array + lookup. The single visible file enumerating all
// lamp variants compiled into this binary. Adding a new variant =
// append one entry to kLampVariants in lamp_variants.cpp + #include
// the variant header there. main.cpp doesn't change.

#pragma once
#include <memory>
#include <string>

namespace lamp {

class Lamp;

// Returns a freshly-constructed Lamp of the named variant, or nullptr
// if the type string isn't in kLampVariants. main.cpp handles the
// nullptr case with FATAL halt + visible LED blink.
std::unique_ptr<Lamp> createLampByType(const std::string& type);

// Comma-separated list of known variant names. Used by main.cpp's
// FATAL halt log so an operator can see what was actually compiled
// into this binary. Returns a pointer to a module-static buffer —
// safe to log; never free.
const char* knownLampTypes();

}  // namespace lamp
