// core/lamp_test_action.cpp: test-panel action dispatcher.
// dispatchLampAction handles WebSocket test actions from the app
// (test_expression, test_zone_preview, test_expression_complete, etc.).
// A ZonePreviewSurface per surface holds the live Zone marker.
// Called from Lamp::drainTestAction() in lamp_drains.cpp.
//
// Sibling TU of lamp.cpp; shares file-scope state via core/lamp_internal.hpp.

#include "core/lamp.hpp"
#include "core/lamp_internal.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "esp_heap_caps.h"
#include "components/network/transport/espnow_link.hpp"
#include "esp_system.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "behaviors/configurator.hpp"
#include "behaviors/greetable.hpp"
#include "behaviors/social.hpp"
#include "behaviors/zone_preview_surface.hpp"
#include "components/apply/apply_brightness.hpp"
#include "components/apply/apply_expressions.hpp"
#include "components/apply/apply_lamp.hpp"
#include "components/network/ble/ble_control.hpp"
#include "components/network/mesh/lamp_roster.hpp"
#include "config/config.hpp"
#include "core/compositor.hpp"
#include "core/moments.hpp"
#include "core/frame_buffer.hpp"
#include "core/override_aggregate.hpp"
#include "core/personality_engine.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_manager.hpp"
#include "expressions/yawn/yawn_expression.hpp"
#include "util/bd_addr.hpp"
#include "util/color.hpp"
#include "util/gradient.hpp"

namespace {

lamp::ZonePreviewSurface s_shadeZonePreview;
lamp::ZonePreviewSurface s_baseZonePreview;

void applyZonePreview(uint32_t posMin, uint32_t posMax,
                      lamp::ExpressionTarget target, lamp::Color color) {
  // The marker composites below the wisp layer; yield the painted surfaces
  // so wisp paint + expressions don't cover it (same gate the color editor
  // uses). Yield tracks the active flags so it can't outlive the preview.
  if (target == lamp::TARGET_SHADE || target == lamp::TARGET_BOTH) {
    s_shadeZonePreview.show(shadeConfiguratorBehavior, shade.pixelCount, posMin, posMax, color);
    lamp::overrides.setSurfacePreview(0x02, s_shadeZonePreview.active());
  }
  if (target == lamp::TARGET_BASE || target == lamp::TARGET_BOTH) {
    s_baseZonePreview.show(baseConfiguratorBehavior, base.pixelCount, posMin, posMax, color);
    lamp::overrides.setSurfacePreview(0x01, s_baseZonePreview.active());
  }
}

void clearZonePreview() {
  if (s_shadeZonePreview.active()) {
    s_shadeZonePreview.release(shadeConfiguratorBehavior);
    lamp::overrides.setSurfacePreview(0x02, false);
  }
  if (s_baseZonePreview.active()) {
    s_baseZonePreview.release(baseConfiguratorBehavior);
    lamp::overrides.setSurfacePreview(0x01, false);
  }
}

}  // namespace

void resetZonePreviewOnDisconnect() { clearZonePreview(); }

void dispatchLampAction(JsonDocument& doc, unsigned long updateTimeMs) {
  lamp::stampConfiguratorActivity(updateTimeMs);

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
        for (JsonPairConst kv : doc["parameters"].as<JsonObjectConst>()) {
          inv.parameters.set(kv.key().c_str(), kv.value().as<uint32_t>());
        }
#ifdef LAMP_DEBUG
        Serial.printf("[test] transient pulse colors=%zu type=%s target=%d\n",
                      inv.colors.size(), inv.type.c_str(),
                      static_cast<int>(target));
#endif
        expressionManager.triggerInvocation(inv, kLocalTestMac, /*broadcast=*/true,
                                            lamp::TransientSource::Local);
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
    const char* lampIdC = doc["lampId"] | "";
    uint8_t peerMac[6];
    lamp::RosterEntry peer;
    auto& ctx = compositor.behaviorContext();
    if (ctx.greeting && lamp::parseBdAddr(lampIdC, peerMac) &&
        lamp::lampRoster.findByMac(peerMac, peer)) {
      if (!ctx.greeting->greetingState().active) {
        ctx.greeting->triggerGreeting(lamp::PeerView::from(peer));
      }
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
    lamp::stampConfiguratorActivity(millis());

    if (doc["shadeColors"]) {
      JsonArray shadeColors = doc["shadeColors"];
      if (shadeColors.size()) {
        std::vector<lamp::Color> updatedColors;
        for (JsonVariant shadeColor : shadeColors) {
          updatedColors.push_back(lamp::hexStringToColor(shadeColor));
        }
        shadeConfiguratorBehavior.showScene(
            lamp::buildRoleScene(shade.pixelCount, updatedColors,
                                 config.shade.renderedRotation()),
            0);
      }
    }
    if (doc["baseColors"]) {
      JsonArray baseColors = doc["baseColors"];
      if (baseColors.size()) {
        std::vector<lamp::Color> updatedColors;
        for (JsonVariant baseColor : baseColors) {
          updatedColors.push_back(lamp::hexStringToColor(baseColor));
        }
        baseConfiguratorBehavior.showScene(
            lamp::buildRoleScene(base.pixelCount, updatedColors,
                                 config.base.renderedRotation()),
            0);
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
  // Personality dev-injection hook: replaces lampRoster view inside
  // PersonalityEngine so a developer can simulate a crowd from one
  // physical lamp + the Flutter app's test-action button. Without this,
  // verifying the 50% crowd-dim floor would need 10 lamps in BLE range
  // simultaneously.
  //
  // Payload:
  //   {"a":"inject_nearby","peers":[
  //     {"name":"red","mac":"AA:BB:CC:DD:EE:01","baseColor":"#FF0000FF","disposition":5},
  //     {"name":"blue","mac":"AA:BB:CC:DD:EE:02","baseColor":"#0000FFFF","disposition":1}
  //   ]}
  // Dispositions are keyed by MAC, so a peer without "mac" counts as Neutral.
  // Injected peers read as seen now and go stale after the freshness window.
  // Pair with {"a":"clear_nearby"} to drop back to live data.
  else if (action == "inject_nearby") {
    std::vector<lamp::NearbyCopy> peers;
    JsonArray arr = doc["peers"];
    for (JsonVariant v : arr) {
      lamp::NearbyCopy p;
      snprintf(p.name, sizeof(p.name), "%s", v["name"] | "");
      if (p.name[0] == '\0') continue;
      const String baseHex = String(v["baseColor"] | "#FFFFFFFF");
      p.baseColor = lamp::hexStringToColor(baseHex.c_str());
      p.lastRssi = v["rssi"].is<int>() ? static_cast<int8_t>(v["rssi"].as<int>()) : -50;
      p.lastSeenNearMs = millis();
      p.lastSeenBleMs = p.lastSeenNearMs;
      const char* mac = v["mac"] | "";
      p.hasMac = lamp::parseBdAddr(mac, p.mac);
      const int disp = v["disposition"] | 3;
      if (p.hasMac && disp >= 1 && disp <= 5) {
        char lampId[18];
        lamp::formatBdAddr(p.mac, lampId);
        config.setDisposition(lampId, static_cast<uint8_t>(disp));
      }
      peers.push_back(p);
    }
    Serial.printf("[personality] inject_nearby count=%u\n", (unsigned)peers.size());
    lamp::personalityEngine.setNearbyOverride(std::move(peers));
  } else if (action == "clear_nearby") {
    Serial.println("[personality] clear_nearby");
    lamp::personalityEngine.clearNearbyOverride();
  }
  // Bench hooks for Moments. "moment" starts a kind now, past eligibility,
  // conditions and the crowd cooldown but never past a suppressor:
  //   {"a":"moment","k":"fireflies|whisper","seed":42}
  else if (action == "moment") {
    const String k = String(doc["k"] | "");
    const uint32_t seed = doc["seed"] | 0u;
    lamp::MomentKind kind;
    if (k == "fireflies") {
      kind = lamp::MomentKind::Fireflies;
    } else if (k == "whisper") {
      kind = lamp::MomentKind::Whisper;
    } else {
      Serial.println("[moment] force err kind");
      return;
    }
    lamp::momentDirector.force(kind, seed, millis());
  } else if (action == "moment_state") {
    lamp::momentDirector.dumpState(millis());
  }
#endif
}

#ifdef LAMP_DEBUG
namespace {

constexpr size_t kSerialCmdMax = 1024;

// `<key> <decimal>`, a tuning command's arguments; ends `key` in place. False
// when either is missing or the value is not a bare decimal.
bool parseTuningArg(char* args, const char*& key, unsigned long& value) {
  char* sep = strchr(args, ' ');
  if (!sep || !isdigit(static_cast<unsigned char>(sep[1]))) return false;
  *sep = '\0';
  char* end = nullptr;
  value = strtoul(sep + 1, &end, 10);
  key = args;
  return *end == '\0';
}

struct YawnArg {
  const char* name;
  const char* key;
  bool fraction;  // typed 0..1, stored as a percent
};
constexpr YawnArg kYawnArgs[] = {
    {"dur", lamp::kYawnDurationKey, false},   {"depth", lamp::kYawnDepthKey, false},
    {"lift", lamp::kYawnLiftKey, false},      {"hold", lamp::kYawnHoldKey, false},
    {"rec", lamp::kYawnRecoverKey, false},    {"warm", lamp::kYawnWarmthKey, true},
    {"lag", lamp::kYawnLagKey, false},        {"opacity", lamp::kOpacityParam.key, false},
};

// `yawn [dur=4500 depth=35 lift=8 hold=500 rec=1800 warm=0.3 lag=200
// surf=shade|base|both]` plays one yawn now as a local transient that no peer
// hears. Absent keys take the descriptor defaults; warm is 0..1.
void runYawnCommand(char* args) {
  static const uint8_t kLocalMac[6] = {0, 0, 0, 0, 0, 0};
  lamp::ExpressionInvocation inv;
  inv.type = lamp::YawnExpression::classDescriptor().id;
  inv.target = lamp::TARGET_BOTH;
  char* save = nullptr;
  for (char* tok = strtok_r(args, " ", &save); tok; tok = strtok_r(nullptr, " ", &save)) {
    char* eq = strchr(tok, '=');
    if (!eq) {
      Serial.printf("[cmd] err yawn arg %s\n", tok);
      return;
    }
    *eq = '\0';
    const char* value = eq + 1;
    if (strcmp(tok, "surf") == 0) {
      if (strcmp(value, "shade") == 0) inv.target = lamp::TARGET_SHADE;
      else if (strcmp(value, "base") == 0) inv.target = lamp::TARGET_BASE;
      else if (strcmp(value, "both") == 0) inv.target = lamp::TARGET_BOTH;
      else {
        Serial.printf("[cmd] err yawn surf %s\n", value);
        return;
      }
      continue;
    }
    const YawnArg* arg = nullptr;
    for (const YawnArg& a : kYawnArgs) {
      if (strcmp(tok, a.name) == 0) arg = &a;
    }
    if (!arg) {
      Serial.printf("[cmd] err yawn key %s\n", tok);
      return;
    }
    const float number = strtof(value, nullptr);
    inv.parameters.set(arg->key, static_cast<uint32_t>(std::max(
                                     0.0f, arg->fraction ? number * 100.0f + 0.5f : number)));
  }

  lamp::ExpressionParams shown = inv.parameters;
  expressionManager.registry().applyDefaults(lamp::YawnExpression::classDescriptor(), shown, 1);
  const bool started = expressionManager.triggerInvocation(inv, kLocalMac, /*broadcast=*/false,
                                                           lamp::TransientSource::Local);
  Serial.printf(
      "[cmd] %s yawn dur=%u depth=%u lift=%u hold=%u rec=%u warm=%.2f lag=%u opacity=%u "
      "target=%u\n",
      started ? "ok" : "err not started", (unsigned)shown.get(lamp::kYawnDurationKey, 0),
      (unsigned)shown.get(lamp::kYawnDepthKey, 0), (unsigned)shown.get(lamp::kYawnLiftKey, 0),
      (unsigned)shown.get(lamp::kYawnHoldKey, 0), (unsigned)shown.get(lamp::kYawnRecoverKey, 0),
      shown.get(lamp::kYawnWarmthKey, 0) / 100.0, (unsigned)shown.get(lamp::kYawnLagKey, 0),
      (unsigned)shown.get(lamp::kOpacityParam.key, 0), (unsigned)inv.target);
}

// Runs on the loop task (Core 1), same task as the pending-slot drains, so
// commands call the drain-side appliers directly instead of posting slots.
void runSerialCommand(char* line, size_t len) {
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) {
    line[--len] = '\0';
  }
  if (len == 0) return;

  if (line[0] == '{') {
    JsonDocument doc;
    if (deserializeJson(doc, line, len) != DeserializationError::Ok) {
      Serial.println("[cmd] err parse");
      return;
    }
    const char* a = doc["a"] | "";
    if (*a == '\0') {
      Serial.println("[cmd] err missing action");
      return;
    }
    dispatchLampAction(doc, millis());
    Serial.printf("[cmd] ok a=%s\n", a);
    return;
  }

  // Stalls the loop task so the loop watchdog's reset can be seen on the rig.
  if (strcmp(line, "loopwd.hang") == 0) {
    Serial.println("[cmd] ok loopwd.hang");
    Serial.flush();
    volatile bool spin = true;
    while (spin) {
    }
  }

  if (strcmp(line, "yawn") == 0 || strncmp(line, "yawn ", 5) == 0) {
    runYawnCommand(line + 4);
    return;
  }

  if (strcmp(line, "heap.get") == 0) {
    Serial.printf("[cmd] ok heap free=%u largest=%u minever=%u txstalls=%lu txrefused=%lu\n",
                  (unsigned)esp_get_free_heap_size(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)esp_get_minimum_free_heap_size(),
                  (unsigned long)lamp::EspNowLink::s_txCredit.stalls(),
                  (unsigned long)lamp::EspNowLink::s_txCredit.refused());
    return;
  }

  if (strcmp(line, "expr.get") == 0) {
    std::string out;
    config.expressionsSectionJsonCached(out);
    // write() streams the JSON; printf would malloc the whole line first.
    Serial.print("[cmd] ok ");
    Serial.write(reinterpret_cast<const uint8_t*>(out.data()), out.size());
    Serial.println();
    return;
  }

  if (strncmp(line, "expr.set ", 9) == 0) {
    JsonDocument doc;
    if (deserializeJson(doc, line + 9) != DeserializationError::Ok) {
      Serial.println("[cmd] err parse");
      return;
    }
    // Same choke point as the BLE CHAR_EXPRESSION_OP drain (drainExpressionOp):
    // expressionOpToConfig per entry, then invalidate + persist.
    size_t applied = 0;
    if (doc.is<JsonArray>()) {
      for (JsonVariant entry : doc.as<JsonArray>()) {
        JsonDocument op;
        op["op"] = "upsert";
        op["entry"] = entry;
        lamp::apply::expressionOpToConfig(op.as<JsonObject>());
        ++applied;
      }
    } else if (doc["op"].is<const char*>()) {
      lamp::apply::expressionOpToConfig(doc.as<JsonObject>());
      applied = 1;
    } else {
      Serial.println("[cmd] err expected section array or op object");
      return;
    }
    config.invalidateExpressionsSection();
    config.persistConfig("expressionOp");
    Serial.printf("[cmd] ok applied=%u\n", (unsigned)applied);
    return;
  }

  if (strcmp(line, "gov.get") == 0) {
    const char* state = "dormant";
    switch (s_powerGovernor.state()) {
      case lamp::PowerGovernor::State::BootRamp: state = "boot"; break;
      case lamp::PowerGovernor::State::Dormant:  state = "dormant"; break;
      case lamp::PowerGovernor::State::Clamped:  state = "clamped"; break;
    }
    Serial.printf("[cmd] ok gov state=%s demand=%.0f age=%u budget=%u ceiling=%u requested=%u\n",
                  state, s_powerGovernor.lastDemandMa(),
                  (unsigned)(millis() - s_powerGovernor.lastSenseMs()),
                  (unsigned)s_powerGovernor.pixelBudgetMa(),
                  (unsigned)s_powerGovernor.ceiling(millis()),
                  (unsigned)lamp::requestedStripLevel());
    return;
  }

  if (strcmp(line, "copy.get") == 0) {
    const lamp::ContagionTuning& t = socialEchoObserver.tuning();
    Serial.printf("[cmd] ok copy copyRollCooldownMs=%lu chainMaxHops=%u chainHopPct=%u "
                  "familiarBoostPct=%u crowdHalf=%u\n",
                  (unsigned long)t.copyRollCooldownMs, (unsigned)t.chainMaxHops,
                  (unsigned)t.chainHopPct, (unsigned)t.familiarBoostPct,
                  (unsigned)t.crowdHalf);
    return;
  }

  // `copy.set <key> <value>`, key a ContagionTuning member; RAM only, so a
  // reboot restores the defaults.
  if (strncmp(line, "copy.set ", 9) == 0) {
    const char* key = nullptr;
    unsigned long value = 0;
    if (!parseTuningArg(line + 9, key, value) ||
        !lamp::setContagionTuning(socialEchoObserver.tuning(), key, value)) {
      Serial.println("[cmd] err copy.set");
      return;
    }
    Serial.printf("[cmd] ok copy.set %s=%lu\n", key, value);
    return;
  }

  if (strcmp(line, "fond.get") == 0) {
    const lamp::FondnessTuning& t = fondnessEngine.tuning;
    const lamp::ContagionTuning& c = socialEchoObserver.tuning();
    Serial.printf("[cmd] ok fond on=%u tracking=%u energy=%u", (unsigned)config.dispositions().engineEnabled(),
                  (unsigned)fondnessEngine.tracking(), (unsigned)t.energy);
    for (size_t i = 0; i < lamp::kEnergyTypeCount; ++i) {
      Serial.printf(" e.%s=%u", lamp::kEnergyTypes[i], (unsigned)t.typeWeight[i]);
    }
    Serial.printf(" eventsPerMin=%u overlap=%u colour=%u hueBand=%u", (unsigned)t.eventsPerMin,
                  (unsigned)t.overlap, (unsigned)t.colour, (unsigned)t.hueBand);
    Serial.printf(" familiar=%u familiarMode=%u,%u,%u meetEveryH=%u", (unsigned)t.familiar,
                  (unsigned)t.familiarMode[0], (unsigned)t.familiarMode[1],
                  (unsigned)t.familiarMode[2], (unsigned)t.meetEveryH);
    Serial.printf(" absence=%u deltaCapPerMin=%u notchFloorMin=%u notchesPerHour=%u",
                  (unsigned)t.absence, (unsigned)t.deltaCapPerMin, (unsigned)t.notchFloorMin,
                  (unsigned)t.notchesPerHour);
    Serial.printf(" copyRollCooldownMs=%lu chainMaxHops=%u chainHopPct=%u",
                  (unsigned long)c.copyRollCooldownMs, (unsigned)c.chainMaxHops,
                  (unsigned)c.chainHopPct);
    Serial.printf(" familiarBoostPct=%u crowdHalf=%u\n", (unsigned)c.familiarBoostPct,
                  (unsigned)c.crowdHalf);
    return;
  }

  // `fond.set <key> <value>`, key a FondnessTuning or ContagionTuning name;
  // RAM only, so a reboot restores the defaults.
  if (strncmp(line, "fond.set ", 9) == 0) {
    char* key = line + 9;
    char* sep = strchr(key, ' ');
    if (sep) *sep = '\0';
    char* end = nullptr;
    const unsigned long value = sep ? strtoul(sep + 1, &end, 10) : 0;
    if (!sep || !isdigit(static_cast<unsigned char>(sep[1])) || *end != '\0' ||
        !(lamp::setFondnessTuning(fondnessEngine.tuning, key, value) ||
          lamp::setContagionTuning(socialEchoObserver.tuning(), key, value))) {
      Serial.println("[cmd] err fond.set");
      return;
    }
    Serial.printf("[cmd] ok fond.set %s=%lu\n", key, value);
    return;
  }

  if (strcmp(line, "fond.on") == 0 || strcmp(line, "fond.off") == 0) {
    const bool on = line[6] == 'n';
    fondnessEngine.setEnabled(on, lamp::uptimeMs());
    Serial.printf("[cmd] ok fond.%s\n", on ? "on" : "off");
    return;
  }

  if (strcmp(line, "fond.peers") == 0) {
    const uint64_t now = lamp::uptimeMs();
    size_t n = 0;
    uint8_t mac[6];
    lamp::PeerRecord r;
    while (config.dispositions().peerAt(n, mac, r, now)) {
      char addr[18];
      lamp::formatBdAddr(mac, addr);
      Serial.printf("[fond] %s disp=%u score=%d", addr, (unsigned)r.disp, (int)r.score);
      Serial.printf(" meets=%u meetAgeH=%u unseenMin=%u\n", (unsigned)r.meets,
                    (unsigned)r.meetAgeH, (unsigned)r.unseenMin);
      ++n;
    }
    Serial.printf("[cmd] ok fond.peers n=%u\n", (unsigned)n);
    return;
  }

  if (strcmp(line, "reunion.get") == 0) {
    const lamp::ReunionTuning& t = shadeSocialBehavior.reunionTuning();
    Serial.printf("[cmd] ok reunion reunionMin=%u reunionX=%u hintCooldownMs=%lu hintOpacity=%u\n",
                  (unsigned)t.reunionMin, (unsigned)t.reunionX,
                  (unsigned long)t.hintCooldownMs, (unsigned)t.hintOpacity);
    return;
  }

  // `reunion.set <key> <value>`, key a ReunionTuning member; RAM only.
  if (strncmp(line, "reunion.set ", 12) == 0) {
    const char* key = nullptr;
    unsigned long value = 0;
    if (!parseTuningArg(line + 12, key, value) ||
        !lamp::setReunionTuning(shadeSocialBehavior.reunionTuning(), key, value)) {
      Serial.println("[cmd] err reunion.set");
      return;
    }
    Serial.printf("[cmd] ok reunion.set %s=%lu\n", key, value);
    return;
  }

  if (strcmp(line, "yawn.get") == 0) {
    const lamp::YawnTuning& t = socialEchoObserver.yawnTuning();
    Serial.printf("[cmd] ok yawn yawnRollCooldownMs=%lu yawnSelfFloorMs=%lu tiredYawnPct=%u "
                  "tiredYawnEveryMs=%lu\n",
                  (unsigned long)t.yawnRollCooldownMs, (unsigned long)t.yawnSelfFloorMs,
                  (unsigned)t.tiredYawnPct, (unsigned long)t.tiredYawnEveryMs);
    Serial.printf("[cmd] ok yawn salty=%u wary=%u neutral=%u fond=%u smitten=%u\n",
                  (unsigned)t.backPct[0], (unsigned)t.backPct[1], (unsigned)t.backPct[2],
                  (unsigned)t.backPct[3], (unsigned)t.backPct[4]);
    return;
  }

  // `yawn.set <key> <value>`, key a YawnTuning member or yawn.<disposition>;
  // RAM only, so a reboot restores the defaults.
  if (strncmp(line, "yawn.set ", 9) == 0) {
    char* key = line + 9;
    char* sep = strchr(key, ' ');
    if (sep) *sep = '\0';
    char* end = nullptr;
    const unsigned long value = sep ? strtoul(sep + 1, &end, 10) : 0;
    if (!sep || !isdigit(static_cast<unsigned char>(sep[1])) || *end != '\0' ||
        !lamp::setYawnTuning(socialEchoObserver.yawnTuning(), key, value)) {
      Serial.println("[cmd] err yawn.set");
      return;
    }
    Serial.printf("[cmd] ok yawn.set %s=%lu\n", key, value);
    return;
  }

  if (strcmp(line, "cfg.get") == 0) {
    std::string base, shade;
    config.baseSectionJsonCached(base);
    config.shadeSectionJsonCached(shade);
    Serial.printf("[cmd] ok base=%s shade=%s\n", base.c_str(), shade.c_str());
    return;
  }

  // Same partial-merge choke point as the BLE settings_blob `lamp` drain, e.g.
  // `cfg.set {"password":""}` clears the control password on a locked-out lamp.
  if (strncmp(line, "cfg.set ", 8) == 0) {
    JsonDocument doc;
    if (deserializeJson(doc, line + 8) != DeserializationError::Ok) {
      Serial.println("[cmd] err parse");
      return;
    }
    lamp::apply::lampLocal(doc.as<JsonObject>(), s_hwMaxBrightness);
    config.persistConfig("cfgSet");
    Serial.println("[cmd] ok cfg.set");
    return;
  }

  Serial.println("[cmd] err unknown");
}

}  // namespace

void pollSerialCommands() {
  static char s_buf[kSerialCmdMax];
  static size_t s_len = 0;
  static bool s_overflow = false;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      if (s_overflow) {
        Serial.println("[cmd] err overflow");
      } else {
        s_buf[s_len] = '\0';
        runSerialCommand(s_buf, s_len);
      }
      s_len = 0;
      s_overflow = false;
    } else if (s_len < kSerialCmdMax - 1) {
      s_buf[s_len++] = c;
    } else {
      s_overflow = true;
    }
  }
}
#endif
