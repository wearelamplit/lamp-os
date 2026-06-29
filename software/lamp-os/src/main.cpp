// software/lamp-os/src/main.cpp
//
// Unified-firmware entry point. Resolves the lamp variant from NVS,
// falls back to the LAMP_INITIAL_TYPE build flag on first boot, then
// to "standard" as the canonical default. Looks the resolved type up
// in lamp_variants.cpp::kLampVariants via createLampByType().
//
// FATAL halt with visible LED blink if the lampType isn't known —
// silent fallback to StandardLamp would be the worst failure mode
// (lamp boots healthy, joins mesh, drives wrong hardware).

#include <Arduino.h>
#include <memory>
#include <string>

#include "config/config.hpp"
#include "config/nvs_config_store.hpp"
#include "core/lamp.hpp"
#include "core/lamp_variants.hpp"

#ifndef LAMP_INITIAL_TYPE
#define LAMP_INITIAL_TYPE ""  // empty = no build-flag seed
#endif

extern lamp::Config config;            // file-scope singleton, defined in lamp.cpp
extern lamp::NvsConfigStore configStore;  // file-scope NVS store, defined in lamp.cpp

static std::unique_ptr<lamp::Lamp> g_lamp;

static std::string resolveLampType() {
  // 1. NVS wins.
  std::string t = config.loadLampType();
  if (!t.empty()) return t;

  // 2. Build-flag seed for first-boot (PIO inject_initial_type.py injects
  //    LAMP_INITIAL_TYPE from the LAMP_TYPE env var, "" otherwise).
  std::string seed = LAMP_INITIAL_TYPE;
  if (!seed.empty()) {
    config.setLampType(seed);   // persist for next boot
    return seed;
  }

  // 2.5. Serial CLI provisioning fallback. When NVS is empty
  //      and no build-flag seed was injected, listen 10s on USB serial
  //      for "LAMP_TYPE=<name>\n". Lets ops provision a fresh hardware
  //      lamp without rebuilding. Unknown name is rejected; we keep
  //      listening until window expires, then fall through to default.
  Serial.println("[lamp] no NVS lampType + no build-flag seed; "
                 "listening 10s for LAMP_TYPE=<name>");
  String line;
  const uint32_t deadline = millis() + 10000;
  while (millis() < deadline) {
    while (Serial.available()) {
      char c = static_cast<char>(Serial.read());
      if (c == '\n' || c == '\r') {
        if (line.startsWith("LAMP_TYPE=")) {
          std::string entered(line.c_str() + 10);
          // Trim trailing whitespace (handles CRLF, stray spaces).
          while (!entered.empty() &&
                 (entered.back() == ' ' || entered.back() == '\t' ||
                  entered.back() == '\r' || entered.back() == '\n')) {
            entered.pop_back();
          }
          if (!entered.empty() && lamp::createLampByType(entered)) {
            config.setLampType(entered);
            Serial.printf("[lamp] persisted lampType=\"%s\" via serial CLI\n",
                          entered.c_str());
            return entered;
          }
          Serial.printf("[lamp] rejecting unknown lampType=\"%s\". Known: %s\n",
                        entered.c_str(), lamp::knownLampTypes());
        }
        line = "";
      } else {
        line += c;
      }
    }
    delay(10);
  }
  Serial.println("[lamp] serial CLI window timed out");

  // 3. Canonical default — persist so subsequent boots skip the
  //    resolution chain entirely (no 10s CLI window on every reboot).
  config.setLampType("standard");
  return "standard";
}

[[noreturn]] static void haltVisible() {
  pinMode(LED_BUILTIN, OUTPUT);
  for (;;) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);
    digitalWrite(LED_BUILTIN, LOW);
    delay(200);
  }
}

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

  const std::string t = resolveLampType();
  g_lamp = lamp::createLampByType(t);
  if (!g_lamp) {
    // Per-variant build: NVS holds the name of a variant not compiled in
    // (e.g. lamp was previously flashed as "standard" and is now being
    // re-flashed with the snafu-only binary). Trust the binary identity
    // (LAMP_INITIAL_TYPE), overwrite NVS, and proceed. Loud log so the
    // developer notices the switch.
    const std::string compiled = LAMP_INITIAL_TYPE;
    if (!compiled.empty() && compiled != t) {
      Serial.printf("[lamp] NVS lampType=\"%s\" not compiled in; "
                    "overwriting with \"%s\" (known: %s)\n",
                    t.c_str(), compiled.c_str(), lamp::knownLampTypes());
      config.setLampType(compiled);
      g_lamp = lamp::createLampByType(compiled);
    }
    if (!g_lamp) {
      // LAMP_INITIAL_TYPE itself isn't in the registry — genuine build bug.
      Serial.printf("[lamp] FATAL: unknown lampType=\"%s\". Known: %s\n",
                    t.c_str(), lamp::knownLampTypes());
      haltVisible();
    }
  }
  Serial.printf("[lamp] resolved lampType=\"%s\"\n",
                config.loadLampType().c_str());
  g_lamp->setup();
}

void loop() { g_lamp->tick(); }
