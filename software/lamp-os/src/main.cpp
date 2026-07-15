// software/lamp-os/src/main.cpp
//
// Unified-firmware entry point. Resolves the lamp variant from NVS; if NVS
// holds a different type from the compiled-in variant (e.g. after a
// cross-variant reflash), overwrites it. Compile error if the compiled variant is unknown.

#include <Arduino.h>
#include <memory>
#include <string>

#include "config/config.hpp"
#include "config/nvs_config_store.hpp"
#include "core/lamp.hpp"
#include "core/lamp_variants.hpp"

extern lamp::Config config;            // file-scope singleton, defined in lamp.cpp
extern lamp::NvsConfigStore configStore;  // file-scope NVS store, defined in lamp.cpp

static std::unique_ptr<lamp::Lamp> g_lamp;

void setup() {
  Serial.begin(115200);
  // Small delay so the boot log isn't lost while the host opens the port.
  delay(50);

  // Attach the store to the file-scope Config so loadLampType / setLampType
  // can hit NVS during the variant-resolution chain below. Lamp::setup()
  // later replaces this Config via the store ctor (which loads the JSON
  // blob); the lampType field is loaded separately by setup()'s explicit
  // loadLampType() call.
  config.setStore(&configStore);

  const char* compiled = lamp::compiledLampType();
  const std::string nvs = config.loadLampType();
  if (nvs != compiled) {
    if (!nvs.empty()) {
      Serial.printf("[lamp] NVS lampType=\"%s\" overwritten with compiled \"%s\"\n",
                    nvs.c_str(), compiled);
    }
    config.setLampType(compiled);
  }
  Serial.printf("[lamp] lampType=\"%s\"\n", compiled);

  g_lamp = lamp::createCompiledLamp();
  g_lamp->setup();
}

void loop() { g_lamp->tick(); }
