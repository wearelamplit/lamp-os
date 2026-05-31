#include "./staff_lamp.hpp"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <OneButton.h>
#include <Preferences.h>

#include <cstdint>
#include <string>

#include "../components/network/bluetooth.hpp"
#ifdef LAMP_MQTT_ENABLED
#include "../components/network/mqtt.hpp"
#endif
#include "../components/network/wifi.hpp"
#include "../expressions/expression_manager.hpp"
#include "../util/color.hpp"
#include "./behaviors/configurator.hpp"
#include "./behaviors/dmx.hpp"
#include "./behaviors/fade_out.hpp"
#include "./behaviors/knockout.hpp"
#include "./behaviors/social.hpp"
#include "./config/config.hpp"
#include "./core/animated_behavior.hpp"
#include "./core/compositor.hpp"
#include "./core/frame_buffer.hpp"
#include "./globals.hpp"
#include "./util/color.hpp"
#include "./util/gradient.hpp"
#include "./util/levels.hpp"
#include "SPIFFS.h"

Adafruit_NeoPixel shadeStrip(LAMP_MAX_STRIP_PIXELS_SHADE, LAMP_SHADE_PIN, NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel baseStrip(LAMP_MAX_STRIP_PIXELS_BASE, LAMP_BASE_PIN, NEO_GRBW + NEO_KHZ800);
Preferences prefs;
uint32_t lastStageModeCheckTimeMs = 0;
uint32_t lastDmxCheckTimeMs = 0;
uint32_t lastArtnetFrameTimeMs = 0;
lamp::ArtnetDetail artnetData;
lamp::BluetoothComponent bt;
lamp::WifiComponent wifi;
lamp::Compositor compositor;
lamp::FrameBuffer shade;
lamp::FrameBuffer base;
lamp::DmxBehavior shadeDmxBehavior;
lamp::DmxBehavior baseDmxBehavior;
lamp::SocialBehavior shadeSocialBehavior;
lamp::ConfiguratorBehavior shadeConfiguratorBehavior;
lamp::ConfiguratorBehavior baseConfiguratorBehavior;
lamp::FadeOutBehavior shadeFadeOutBehavior;
lamp::FadeOutBehavior baseFadeOutBehavior;
lamp::KnockoutBehavior baseKnockoutBehavior;
lamp::ExpressionManager expressionManager;
lamp::Config config;
#ifdef LAMP_MQTT_ENABLED
lamp::MqttComponent mqtt;
bool mqttPowerState = true;
#endif
unsigned long lastHomeModeUpdateMs = 0;
bool lastHomeMode = false;  // Track previous home mode state

lamp::Color moodColor;
uint32_t moodWW;
float moodHue;                                            // start from actual shade color
static constexpr float MOOD_HUE_DEGREES_PER_TICK = 0.2f;  // hue shift speed

bool locked = true;
bool unlock1 = false;
uint32_t pressStartTime = 0;
uint32_t key1Time = 0;

bool moody = false;   // whether moody mode is active
bool stoked = false;  // Track if stoked
bool beingTouched = false;

uint32_t touchThreshold = 400;

OneButton stoke(LAMP_STOKE_PIN, false);

uint8_t shadeCurrentBrightness = lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, config.lamp.brightness);
uint8_t baseCurrentBrightness = config.lamp.brightness;
int8_t shadeDimmingDir = 1;
int8_t baseDimmingDir = 1;

uint32_t touchStartTime = 0;
uint32_t lastBrightnessChange = 0;
uint32_t brightnessStepTime = 100;
/**
 * Calculate effective home mode based on configuration and network presence
 */
bool calculateEffectiveHomeMode(lamp::Config& config) {
  if (!config.lamp.homeMode) return false;            // Mode disabled
  if (config.lamp.homeModeSSID.empty()) return true;  // No SSID = always home
  return wifi.isHomeNetworkVisible();                 // Check if home network visible
}

void initBehaviors() {
  shadeDmxBehavior = lamp::DmxBehavior(&shade, 480);
  baseDmxBehavior = lamp::DmxBehavior(&base, 480);
  shadeSocialBehavior = lamp::SocialBehavior(&shade, 1200);
  shadeSocialBehavior.setBluetoothComponent(&bt);
  shadeConfiguratorBehavior = lamp::ConfiguratorBehavior(&shade, 120);
  shadeConfiguratorBehavior.colors = shade.defaultColors;
  baseConfiguratorBehavior = lamp::ConfiguratorBehavior(&base, 120);
  baseConfiguratorBehavior.colors = base.defaultColors;
  shadeFadeOutBehavior = lamp::FadeOutBehavior(&shade, REBOOT_ANIMATION_FRAMES);
  shadeFadeOutBehavior.setWifiComponent(&wifi);
  baseFadeOutBehavior = lamp::FadeOutBehavior(&base, REBOOT_ANIMATION_FRAMES);
  baseFadeOutBehavior.setWifiComponent(&wifi);
  baseKnockoutBehavior = lamp::KnockoutBehavior(&base, 0, true);
  baseKnockoutBehavior.knockoutPixels = config.base.knockoutPixels;

  // Initialize expressions
  expressionManager.begin(&shade, &base);
  expressionManager.loadFromConfig(config.expressions);

  // Build behavior vector in priority order (lowest to highest)
  // Behaviors run in sequence, so later ones override earlier ones
  std::vector<lamp::AnimatedBehavior*> allBehaviors = {};

  // Add expression behaviors (lowest priority - automated effects)
  auto exprBehaviors = expressionManager.getBehaviors();
  allBehaviors.insert(allBehaviors.end(), exprBehaviors.begin(), exprBehaviors.end());

  // Add DMX behaviors (middle priority - live control)
  allBehaviors.push_back(&baseDmxBehavior);
  allBehaviors.push_back(&shadeDmxBehavior);

  // Add social greeting behaviors (high priority)
  allBehaviors.push_back(&shadeSocialBehavior);

  // Add configurator behaviors (highest priority - UI preview, overrides DMX and social)
  allBehaviors.push_back(&baseConfiguratorBehavior);
  allBehaviors.push_back(&shadeConfiguratorBehavior);

  // Add fade behaviors (always last - startup/shutdown effects)
  allBehaviors.push_back(&baseFadeOutBehavior);
  allBehaviors.push_back(&shadeFadeOutBehavior);

  // layers load in priority sequence {lowest, ..., highest}
  compositor.begin(allBehaviors, {&shade, &base}, calculateEffectiveHomeMode(config));

  // Add overlay behaviors
  compositor.overlayBehaviors.push_back(&baseKnockoutBehavior);

  // Set global compositor for expressions
  lamp::setGlobalCompositor(&compositor);

  // Set global expression manager for inter-expression communication
  lamp::setGlobalExpressionManager(&expressionManager);
}

void handleArtnet() {
  uint32_t now = millis();

  if (now > lastDmxCheckTimeMs + 2) {
    lastDmxCheckTimeMs = now;
    lastArtnetFrameTimeMs = wifi.getLastArtnetFrameTimeMs();
    artnetData = wifi.getArtnetData();

    shadeDmxBehavior.setColor(artnetData.shadeColor);
    shadeDmxBehavior.setLastArtnetFrameTimeMs(lastArtnetFrameTimeMs);
    baseDmxBehavior.setColor(artnetData.baseColor);
    baseDmxBehavior.setLastArtnetFrameTimeMs(lastArtnetFrameTimeMs);
  }
};

void handleStageMode() {
  uint32_t now = millis();

  if (now > lastStageModeCheckTimeMs + 2000) {
    lastStageModeCheckTimeMs = now;
    auto foundStages = bt.getStages();

    if (wifi.stageMode && foundStages->size() == 0) {
      wifi.toApMode();
    } else if (!wifi.stageMode && foundStages->size() > 0) {
      wifi.toStageMode(foundStages->at(0).ssid, foundStages->at(0).password);
    }
  }
}

/**
 * Parse ExpressionConfig from JSON object using generic parameter system
 */
lamp::ExpressionConfig parseExpressionConfig(JsonObject node) {
  lamp::ExpressionConfig expr;
  expr.type = std::string(node["type"] | "");
  expr.enabled = node["enabled"] | false;
  expr.intervalMin = node["intervalMin"] | 60;
  expr.intervalMax = node["intervalMax"] | 900;
  expr.target = node["target"] | 3;

#ifdef LAMP_DEBUG
  Serial.printf("Parsing expression: type=%s, enabled=%d, intervalMin=%lu, intervalMax=%lu\n",
                expr.type.c_str(), expr.enabled, expr.intervalMin, expr.intervalMax);
#endif

  // Parse colors
  JsonArray colors = node["colors"];
  if (colors.size()) {
    for (JsonVariant color : colors) {
      expr.colors.push_back(lamp::hexStringToColor(color));
    }
  }

  // Parse generic parameters - store any additional fields as parameters
  for (JsonPair kv : node) {
    const char* key = kv.key().c_str();
    std::string keyStr(key);

    // Skip common fields we've already handled
    if (keyStr == "type" || keyStr == "enabled" || keyStr == "intervalMin" ||
        keyStr == "intervalMax" || keyStr == "target" || keyStr == "colors") {
      continue;
    }

    // Store the parameter value
    JsonVariant value = kv.value();
    if (value.is<uint32_t>()) {
      expr.setParameter(keyStr, value.as<uint32_t>());
    } else if (value.is<int>()) {
      expr.setParameter(keyStr, static_cast<uint32_t>(value.as<int>()));
    }

#ifdef LAMP_DEBUG
    Serial.printf("Stored parameter: %s\n", keyStr.c_str());
#endif
  }

  return expr;
}

void handleWebSocket() {
  if (wifi.hasWebSocketData()) {
    JsonDocument doc = wifi.getWebSocketData();
    shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = wifi.getLastWebSocketUpdateTimeMs();
    baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = wifi.getLastWebSocketUpdateTimeMs();

    // parse the ws action id (a) into a String
    String action = String(doc["a"]);
    if (action == "bright") {
      int level = doc["v"] | 100;
      // Apply immediately for real-time control
      shadeStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, level));
      baseStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, level));
#ifdef LAMP_MQTT_ENABLED
      mqtt.publishState();
#endif
    } else if (action == "knockout") {
      int pixelIndex = doc["p"];
      int percentage = doc["b"];
      if (pixelIndex >= 0 && pixelIndex < 50 && percentage >= 0 && percentage <= 100) {
        baseKnockoutBehavior.knockoutPixels[pixelIndex] = percentage;
        config.base.knockoutPixels[pixelIndex] = percentage;
      }
    } else if (action == "base") {
      JsonArray baseColors = doc["c"];
      if (baseColors.size()) {
        std::vector<lamp::Color> updatedColors;
        for (JsonVariant baseColor : baseColors) {
          updatedColors.push_back(lamp::hexStringToColor(baseColor));
        }
        baseConfiguratorBehavior.colors = lamp::buildGradientWithStops(base.pixelCount, updatedColors);
      }
    } else if (action == "shade") {
      JsonArray shadeColors = doc["c"];
      if (shadeColors.size()) {
        std::vector<lamp::Color> updatedColors;
        for (JsonVariant shadeColor : shadeColors) {
          updatedColors.push_back(lamp::hexStringToColor(shadeColor));
        }
        shadeConfiguratorBehavior.colors = lamp::buildGradientWithStops(shade.pixelCount, updatedColors);
      }
    } else if (action == "test_expression") {
      String type = String(doc["type"]);
      if (type.length() > 0) {
#ifdef LAMP_DEBUG
        Serial.printf("Testing expression: %s\n", type.c_str());
#endif
        // Disable configurator during expression test so expression shows against actual base colors
        shadeConfiguratorBehavior.disabled = true;
        baseConfiguratorBehavior.disabled = true;
        expressionManager.triggerExpression(type.c_str());
      }
    } else if (action == "test_expression_complete") {
      // Re-enable configurator after test expression completes
      shadeConfiguratorBehavior.disabled = false;
      baseConfiguratorBehavior.disabled = false;
      // Reset timer to keep preview active
      shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
      baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();

      // Restore preview colors
      if (doc["shadeColors"]) {
        JsonArray shadeColors = doc["shadeColors"];
        if (shadeColors.size()) {
          std::vector<lamp::Color> updatedColors;
          for (JsonVariant shadeColor : shadeColors) {
            updatedColors.push_back(lamp::hexStringToColor(shadeColor));
          }
          shadeConfiguratorBehavior.colors = lamp::buildGradientWithStops(shade.pixelCount, updatedColors);
        }
      }
      if (doc["baseColors"]) {
        JsonArray baseColors = doc["baseColors"];
        if (baseColors.size()) {
          std::vector<lamp::Color> updatedColors;
          for (JsonVariant baseColor : baseColors) {
            updatedColors.push_back(lamp::hexStringToColor(baseColor));
          }
          baseConfiguratorBehavior.colors = lamp::buildGradientWithStops(base.pixelCount, updatedColors);
        }
      }
    }
  }
}

void applyMoodColorToShade() {
  auto filled = lamp::buildGradientWithStops(shade.pixelCount, {moodColor});
  shadeConfiguratorBehavior.colors = filled;
  shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
}

void stokePoke() {
  if (!stoked) {
    shadeCurrentBrightness = baseCurrentBrightness = LAMP_MAX_BRIGHTNESS;
    shadeStrip.setBrightness(shadeCurrentBrightness);
    baseStrip.setBrightness(baseCurrentBrightness);

  } else {
    shadeCurrentBrightness = baseCurrentBrightness = config.lamp.brightness;
    shadeStrip.setBrightness(shadeCurrentBrightness);
    baseStrip.setBrightness(baseCurrentBrightness);
  }
  stoked = !stoked;
}

void moodToggle() {
  if (!locked) {
    moody = !moody;
    if (moody) {
      applyMoodColorToShade();
    } else {
      // Hand back to normal colors — restore config shade colors into configurator
      auto restored = lamp::buildGradientWithStops(shade.pixelCount, config.shade.colors);
      shadeConfiguratorBehavior.colors = restored;
      shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    }
  } else
    unlock1 = false;
}
void unlockKey1() {
  uint32_t pressedTime = millis() - pressStartTime;
  if (2000 <= pressedTime && pressedTime <= 4000) {
    shadeCurrentBrightness -= 30;
    adjustBrightness();
    Serial.println("Check1");
    key1Time = millis();
  }
}

void moodShift() {
  if (!locked) {
    moodHue += MOOD_HUE_DEGREES_PER_TICK;
    if (moodHue >= 360.0f) moodHue -= 360.0f;
    moodColor = lamp::hsvToColor(moodHue, moodWW);
    if (moody) {
      applyMoodColorToShade();  // live-update the shade if moody is already on
      delay(10);
    }
  } else if (unlock1 == false) {  // unlock sequence start
    shadeCurrentBrightness += 10;
    Serial.println("Unlock sequence start");
    adjustBrightness();
    Serial.println("blink?");
    pressStartTime = millis();
    unlock1 = true;
  }
}

/*void topTouch(void) {
  Serial.println("Touch triggered!");
  if (millis() > 1000 && touchRead(LAMP_TOPTOUCH_PIN) < touchThreshold) {
    Serial.println("I'm touched!");
    moodHue = 0;
    moodColor = lamp::hsvToColor(moodHue, moodWW);
    applyMoodColorToShade();  // live-update the shade if moody is already on
    moody = true;
    delay(10);
  }
}*/

void adjustBrightness(void) {
  stoked = false;
  if (shadeCurrentBrightness >= 100) shadeDimmingDir = -1;
  if (shadeCurrentBrightness <= 5) shadeDimmingDir = 1;
  shadeCurrentBrightness += shadeDimmingDir;
  shadeStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, shadeCurrentBrightness));
  Serial.printf("Shade Brightness Now = %d\n", shadeCurrentBrightness);
  uint32_t now = millis();
  shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = now;
  lastDmxCheckTimeMs = now;
  lastStageModeCheckTimeMs = now;
  lastBrightnessChange = now;
}

void setup() {
#ifdef LAMP_DEBUG
  Serial.begin(115200);
#endif
  config = lamp::Config(&prefs);
  SPIFFS.begin(true);
  bt.begin(config.lamp.name, config.base.colors[config.base.ac], config.shade.colors[0]);
  wifi.begin(&config);
  shadeStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, config.lamp.brightness));
  baseStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, config.lamp.brightness));
  shade.begin(lamp::buildGradientWithStops(config.shade.px, config.shade.colors), config.shade.px, &shadeStrip);
  base.begin(lamp::buildGradientWithStops(config.base.px, config.base.colors), config.base.px, &baseStrip);
  initBehaviors();

#ifdef LAMP_MQTT_ENABLED
  mqtt.begin(
      &config, &wifi,
      // brightness callback - called when HA changes brightness
      [](uint8_t level) {
        shadeStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, level));
        baseStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, level));
      },
      // power callback - called when HA toggles power
      [](bool on) {
        mqttPowerState = on;
        uint8_t level = on ? config.lamp.brightness : 0;
        shadeStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, level));
        baseStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, level));
      });
#endif

  moodColor = config.shade.colors[0];
  moodWW = moodColor.w;
  moodHue = lamp::colorToHue(moodColor);  // start from actual shade color

  stoke.attachClick(stokePoke);
  stoke.attachDoubleClick(moodToggle);
  stoke.attachDuringLongPress(moodShift);
  stoke.attachLongPressStop(unlockKey1);
  stoke.setDebounceMs(80);

  // touchAttachInterrupt(LAMP_TOPTOUCH_PIN, topTouch, touchThreshold);
  // touchInterruptSetThresholdDirection(true);
};

void loop() {
  handleStageMode();
  handleArtnet();
  handleWebSocket();
  wifi.tick();
  stoke.tick();

  // Serial.printf("Top Touch: %ld, Btm Touch: %ld \n", touchRead(LAMP_TOPTOUCH_PIN), touchRead(LAMP_BTMTOUCH_PIN));
  if (!locked) {
    if (millis() > 1000) {
      if (touchRead(LAMP_TOPTOUCH_PIN) < touchThreshold) {
        if (!beingTouched) {
          touchStartTime = millis();
          beingTouched = true;
          Serial.printf("I feel it!! Initial Brightness = %d\n", shadeCurrentBrightness);
          adjustBrightness();
        }
        if (millis() - lastBrightnessChange > brightnessStepTime) {
          Serial.printf("Yay! Pet me!! Initial Brightness = %d\n", shadeCurrentBrightness);
          adjustBrightness();
        }
        //
      }

      if (touchRead(LAMP_BTMTOUCH_PIN) < touchThreshold) {
        Serial.printf("Stroke the stick, mmmmmmm yeah... start = %d\n", baseCurrentBrightness);
        if (baseCurrentBrightness >= 100 || baseCurrentBrightness <= 0) baseDimmingDir *= -1;
        baseCurrentBrightness += baseDimmingDir;

        baseStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, baseCurrentBrightness));
        Serial.printf("Thanks... End = %d\n", baseCurrentBrightness);
        lastBrightnessChange = millis();
        // baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
        //  delay(10);
      }
    }
  } else if (unlock1) {
    if (millis() - key1Time <= 2500 && touchRead(LAMP_TOPTOUCH_PIN) < touchThreshold) {
      Serial.println("Too Soon!");
      locked = true;
      unlock1 = false;
    } else if (millis() - key1Time <= 4000 && touchRead(LAMP_TOPTOUCH_PIN) < touchThreshold) {
      Serial.println("Check 2!");
      locked = false;
      moodColor = lamp::hsvToColor(120, 0);
      applyMoodColorToShade();
    }
  }

#ifdef LAMP_MQTT_ENABLED
  mqtt.tick(wifi.isHomeNetworkVisible());
#endif

  // Update compositor home mode state periodically for social behaviors
  static constexpr uint32_t HOME_MODE_UPDATE_INTERVAL_MS = 30000;
  if (millis() - lastHomeModeUpdateMs > HOME_MODE_UPDATE_INTERVAL_MS) {
    bool effectiveHomeMode = calculateEffectiveHomeMode(config);
    compositor.setHomeMode(effectiveHomeMode);

    // Apply brightness when home mode state changes
    if (effectiveHomeMode != lastHomeMode) {
      uint8_t targetBrightness = effectiveHomeMode ? config.lamp.homeModeBrightness : config.lamp.brightness;

      shadeStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, targetBrightness));
      baseStrip.setBrightness(lamp::calculateBrightnessLevel(LAMP_MAX_BRIGHTNESS, targetBrightness));

      lastHomeMode = effectiveHomeMode;
    }

    lastHomeModeUpdateMs = millis();
  }

  compositor.tick();
};
