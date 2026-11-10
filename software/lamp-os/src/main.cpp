// software/lamp-os/src/main.cpp
//
// Unified-firmware entry point. Resolves the lamp variant from NVS; if NVS
// holds a different type from the compiled-in variant (e.g. after a
// cross-variant reflash), overwrites it. Compile error if the compiled variant is unknown.

#include <Arduino.h>
#include <cstdio>
#include <memory>
#include <string>

#include "config/config.hpp"
#include "config/nvs_config_store.hpp"
#include "core/lamp.hpp"
#include "lamp_variants.hpp"
#include "util/loop_watchdog.hpp"

extern lamp::Config config;            // file-scope singleton, defined in lamp.cpp
extern lamp::NvsConfigStore configStore;  // file-scope NVS store, defined in lamp.cpp

static std::unique_ptr<lamp::Lamp> g_lamp;

// Loop-task stack. Measured HWM peak ~4.4 KB; 7168 leaves ~2.7 KB headroom.
SET_LOOP_TASK_STACK_SIZE(7168);

void setup() {
#ifdef LAMP_DEBUG
  // Serial command ingress: a long command line must survive a loop stall
  // (NVS persist blocks ~tens of ms); the 256-byte default ring tears it.
  Serial.setRxBufferSize(2048);
#endif
  Serial.begin(115200);
  // The UART VFS driver creates its write lock on the first stdout write, and
  // newlib aborts if that allocation fails. Every lamp log goes through
  // Serial, so the first stdout write is otherwise an IDF driver's error line
  // (the WiFi/coex timer task) on a starved heap. Write once now, while the
  // heap is whole.
  fputs("\n", stdout);
  // Small delay so the boot log isn't lost while the host opens the port.
  delay(50);

  // Before anything opens NVS: a partition that reports itself unusable is
  // erased so the lamp still boots with Bluetooth.
  lamp::NvsConfigStore::initFlash();

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
  config.setLampVariant(lamp::compiledLampVariant());

  g_lamp = lamp::createCompiledLamp();
  g_lamp->setup();
  lamp::loop_watchdog::begin();
}

void loop() {
  lamp::loop_watchdog::startIteration();
  g_lamp->tick();
}
