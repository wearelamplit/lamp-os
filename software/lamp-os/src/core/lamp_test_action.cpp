// core/lamp_test_action.cpp: test-panel action dispatcher.
// dispatchLampAction handles WebSocket test actions from the app
// (test_expression, test_zone_preview, test_expression_complete, etc.).
// ZonePreview helpers manage live zone-preview state + restore.
// Called from Lamp::drainTestAction() in lamp_drains.cpp.
//
// Sibling TU of lamp.cpp; shares file-scope state via core/lamp_internal.hpp.

#include "core/lamp.hpp"
#include "core/lamp_internal.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "behaviors/configurator.hpp"
#include "behaviors/greetable.hpp"
#include "components/network/ble/ble_control.hpp"
#include "components/network/mesh/nearby_lamps.hpp"
#include "config/config.hpp"
#include "core/compositor.hpp"
#include "core/frame_buffer.hpp"
#include "core/override_aggregate.hpp"
#include "core/personality_engine.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_manager.hpp"
#include "expressions/zone_preview.hpp"
#include "util/color.hpp"
#include "util/gradient.hpp"

namespace {

// Live Zone-preview state per surface. The saved buffer is the configurator
// baseline snapshotted on the first preview of a drag so clearZonePreview()
// restores it even when the release payload carries no colors.
struct ZonePreview {
  bool shadeActive = false;
  bool baseActive = false;
  std::vector<lamp::Color> savedShade;
  std::vector<lamp::Color> savedBase;
};
ZonePreview s_zonePreview;

void paintZonePreview(lamp::ConfiguratorBehavior& cfg, uint16_t pixelCount,
                      bool& active, std::vector<lamp::Color>& saved,
                      uint32_t posMin, uint32_t posMax, lamp::Color color) {
  if (pixelCount == 0) return;
  if (!active) {
    saved = cfg.colors;
    active = true;
  }
  cfg.beginFade(lamp::buildZonePreviewBuffer(pixelCount, posMin, posMax, color),
                0);
}

void applyZonePreview(uint32_t posMin, uint32_t posMax,
                      lamp::ExpressionTarget target, lamp::Color color) {
  if (target == lamp::TARGET_SHADE || target == lamp::TARGET_BOTH) {
    paintZonePreview(shadeConfiguratorBehavior, shade.pixelCount,
                     s_zonePreview.shadeActive, s_zonePreview.savedShade,
                     posMin, posMax, color);
  }
  if (target == lamp::TARGET_BASE || target == lamp::TARGET_BOTH) {
    paintZonePreview(baseConfiguratorBehavior, base.pixelCount,
                     s_zonePreview.baseActive, s_zonePreview.savedBase,
                     posMin, posMax, color);
  }
}

void clearZonePreview() {
  if (s_zonePreview.shadeActive) {
    shadeConfiguratorBehavior.beginFade(s_zonePreview.savedShade, 0);
    s_zonePreview.savedShade.clear();
    s_zonePreview.shadeActive = false;
  }
  if (s_zonePreview.baseActive) {
    baseConfiguratorBehavior.beginFade(s_zonePreview.savedBase, 0);
    s_zonePreview.savedBase.clear();
    s_zonePreview.baseActive = false;
  }
}

}  // namespace

void resetZonePreviewOnDisconnect() { clearZonePreview(); }

void dispatchLampAction(JsonDocument& doc, unsigned long updateTimeMs) {
  shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = updateTimeMs;
  baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = updateTimeMs;

  String action = String(doc["a"]);
  if (action == "test_expression") {
    String type = String(doc["type"]);
    if (type.length() > 0) {
      lamp::ExpressionTarget target = doc["target"].is<int>()
        ? static_cast<lamp::ExpressionTarget>(doc["target"].as<int>())
        : lamp::TARGET_BOTH;
      // Build payloadColors inline (jsonArrayToColors stays static in lamp.cpp).
      std::vector<lamp::Color> payloadColors;
      for (JsonVariant v : doc["colors"].as<JsonArray>()) {
        payloadColors.push_back(lamp::hexStringToColor(v));
      }
      if (!payloadColors.empty()) {
        // Colors provided → run a transient one-shot seeded with those colors.
        // Works on factory lamps with zero configured expressions (no lookup).
        // Zero MAC is the local-test sentinel; triggerInvocation coalesces
        // rapid re-fires from the same (srcMac, type) pair so spam-tapping
        // Test doesn't pile up transients.
        static const uint8_t kLocalTestMac[6] = {0, 0, 0, 0, 0, 0};
        lamp::ExpressionInvocation inv;
        inv.type = type.c_str();
        inv.target = static_cast<uint8_t>(target);
        inv.colors = std::move(payloadColors);
#ifdef LAMP_DEBUG
        Serial.printf("[test] transient pulse colors=%zu type=%s target=%d\n",
                      inv.colors.size(), inv.type.c_str(),
                      static_cast<int>(target));
#endif
        expressionManager.triggerInvocation(inv, kLocalTestMac);
        expressionManager.markTestActive(type.c_str(), target);
      } else {
        // No colors payload → trigger an already-configured expression by name.
        // This is the expression-editor "test a saved expression" flow.
#ifdef LAMP_DEBUG
        auto cfgColors = expressionManager.getExpressionColors(type.c_str());
        String colorList;
        for (const auto& c : cfgColors) {
          if (colorList.length() > 0) colorList += " ";
          colorList += lamp::colorToHexString(c).c_str();
        }
        Serial.printf("[test] configured trigger: %s target=%d [%s]\n",
                      type.c_str(), static_cast<int>(target), colorList.c_str());
#endif
        expressionManager.triggerExpression(type.c_str(), target);
        if (expressionManager.markTestActive(type.c_str(), target)) {
          ble_control::notifyStateChange();
        }
      }
    }
  } else if (action == "test_zone_preview") {
    const int posMin = doc["posMin"] | -1;
    const int posMax = doc["posMax"] | -1;
    const int target = doc["target"] | static_cast<int>(lamp::TARGET_BOTH);
    const char* colorHex = doc["color"];
    if (posMin >= 0 && posMax >= 0 && colorHex) {
      applyZonePreview(static_cast<uint32_t>(posMin),
                       static_cast<uint32_t>(posMax),
                       static_cast<lamp::ExpressionTarget>(target),
                       lamp::hexStringToColor(colorHex));
    }
  } else if (action == "triggerGreet") {
    const char* bdAddrC = doc["bdAddr"] | "";
    std::string bdAddr(bdAddrC);
    lamp::NearbyLamp peer;
    auto& ctx = compositor.behaviorContext();
    if (!bdAddr.empty() && ctx.greeting && lamp::nearbyLamps.findByBdAddr(bdAddr, peer)) {
      ctx.greeting->triggerGreeting(peer);
    }
  } else if (action == "test_expression_complete") {
    // Clear the active-test set first so the immediate state-notify carries
    // previewActive=false. Stops continuous expressions (breathing, shifty)
    // is the responsibility of the configurator re-asserting baseline below.
    if (expressionManager.clearAllTestActive()) {
      ble_control::notifyStateChange();
    }
    // Restore any live Zone preview before the color rebuild below. An
    // app-supplied colors payload overwrites the restored baseline. An empty
    // payload leaves the pre-preview baseline standing so a preview can't stick.
    clearZonePreview();
    // App may have edited saved colors during the test. Snap configurator
    // to the new values + re-assert any active wisp paint so the new
    // baseline doesn't briefly stomp it.
    shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();

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
    // If the wisp was painting either surface before the expression
    // test, the configurator-color writes just above stomped the wisp's
    // target gradient with the lamp's saved colors. Re-assert the wisp
    // paint immediately so the surface returns to what it was showing
    // pre-test, rather than waiting up to ~10s for the wisp's next
    // backstop paint cycle. No-op when the override isn't in Holding.
    lamp::overrides.shade.reassertHold();
    lamp::overrides.base.reassertHold();
  }
#ifdef LAMP_DEBUG
  // Personality dev-injection hook: replaces nearbyLamps view inside
  // PersonalityEngine so a developer can simulate a crowd from one
  // physical lamp + the Flutter app's test-action button. Without this,
  // verifying the 50% crowd-dim floor would need 10 lamps in BLE range
  // simultaneously.
  //
  // Payload:
  //   {"a":"inject_nearby","peers":[
  //     {"name":"red","baseColor":"#FF0000FF","disposition":5},
  //     {"name":"blue","baseColor":"#0000FFFF","disposition":1}
  //   ]}
  // Pair with {"a":"clear_nearby"} to drop back to live data.
  else if (action == "inject_nearby") {
    std::vector<lamp::NearbyLamp> peers;
    JsonArray arr = doc["peers"];
    for (JsonVariant v : arr) {
      lamp::NearbyLamp p;
      p.name = String(v["name"] | "").c_str();
      if (p.name.empty()) continue;
      const String baseHex = String(v["baseColor"] | "#FFFFFFFF");
      p.baseColor = lamp::hexStringToColor(baseHex.c_str());
      p.lastRssi = v["rssi"].is<int>() ? static_cast<int8_t>(v["rssi"].as<int>()) : -50;
      const int disp = v["disposition"] | 3;
      if (disp >= 1 && disp <= 5 && !p.name.empty()) {
        config.setDisposition(p.name, static_cast<uint8_t>(disp));
      }
      peers.push_back(p);
    }
    Serial.printf("[personality] inject_nearby count=%u\n", (unsigned)peers.size());
    lamp::personalityEngine.setNearbyOverride(std::move(peers));
  } else if (action == "clear_nearby") {
    Serial.println("[personality] clear_nearby");
    lamp::personalityEngine.clearNearbyOverride();
  }
#endif
}
