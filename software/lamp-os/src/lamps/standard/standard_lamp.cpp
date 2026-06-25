// software/lamp-os/src/lamps/standard/standard_lamp.cpp
//
// StandardLamp is a thin subclass of lamp::Lamp. The class body lives in
// standard_lamp.hpp; this TU exists only to give the subclass a translation
// unit. The framework guts (BLE GATT, ESP-NOW mesh, OTA self-health,
// compositor, pending + override aggregates, built-in behaviors) live in
// core/lamp.cpp.

#include "standard_lamp.hpp"
