#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "components/input/button.hpp"
#include "components/input/input_source.hpp"
#include "components/input/touch.hpp"

namespace lamp {
struct InputSpec;

namespace input {

// Build one driver per InputSpec. Firmware-only (reads pins); the returned
// sources are owned by the caller (Lamp) and ticked once per loop.
std::vector<std::unique_ptr<InputSource>> buildInputs(
    const std::vector<InputSpec>& specs);

}  // namespace input
}  // namespace lamp
