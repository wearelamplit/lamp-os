#include "status/status_emitter.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <lampos/led_types.hpp>

#include "paint/current_palette.hpp"
#include "net/eth_link.hpp"
#include "net/mesh_link.hpp"
#include "net/wifi_link.hpp"
#include "config/wisp_config.hpp"
#include "core/wisp_controller.hpp"
#include "wire/lamp_protocol.hpp"
#include "status/status_json.hpp"
#include "status/seq_source.hpp"
#include "util/fnv1a.hpp"

namespace wisp {

namespace {

constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr size_t kPaletteIdPrefixLen = lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN;

}  // namespace

void StatusEmitter::begin(MeshLink* mesh, const WispController* controller,
                          WispConfig* config, CurrentPalette* palette,
                          SeqSource* seq, WifiLink* wifi) {
  mesh_ = mesh;
  controller_ = controller;
  config_ = config;
  palette_ = palette;
  seq_ = seq;
  wifi_ = wifi;
}

void StatusEmitter::startTimer() {
  if (!statusTimer_) {
    statusTimer_ = xTimerCreate(
        "wisp_status",
        pdMS_TO_TICKS(kStatusIntervalMs),
        pdTRUE,  // auto-reload
        this,
        [](TimerHandle_t t) {
          auto* self = static_cast<StatusEmitter*>(pvTimerGetTimerID(t));
          if (self) self->statusDue_.store(true, std::memory_order_relaxed);
        });
    if (statusTimer_) {
      xTimerStart(statusTimer_, 0);
    } else {
      Serial.println("[wisp.beacon] xTimerCreate(status) failed");
    }
  }
}

void StatusEmitter::triggerOnChange() {
  statusDue_.store(true, std::memory_order_relaxed);
  if (statusTimer_) {
    xTimerReset(statusTimer_, 0);
  }
  burstUntilMs_ = millis() + kStatusBurstMs;
}

void StatusEmitter::pollConfig() {
  configPollDue_.store(true, std::memory_order_relaxed);
}

bool StatusEmitter::pump() {
  checkConnEdges();
  const uint32_t now = millis();
  configCadence_.retire(now);
  if (configPollDue_.exchange(false, std::memory_order_relaxed)) {
    configCadence_.poll();
  }
  bool bursting = false;
  if (burstUntilMs_ != 0) {
    if (static_cast<int32_t>(now - burstUntilMs_) < 0) {
      bursting = true;
    } else {
      burstUntilMs_ = 0;
    }
  }
  if (statusDue_.load(std::memory_order_relaxed) || bursting) {
    const uint32_t minInterval = bursting ? kStatusBurstIntervalMs : kMinEmitIntervalMs;
    if ((!haveEmitted_ || now - lastEmitMs_ >= minInterval) &&
        configCadence_.statusClear(now)) {
      statusDue_.store(false, std::memory_order_relaxed);
      haveEmitted_ = true;
      lastEmitMs_ = now;
      emitStatus();
      char configJson[kConfigJsonBufLen];
      const size_t n = buildConfig(configJson, sizeof(configJson));
      configCadence_.onStatusEmitted(
          now, fnv1a(reinterpret_cast<const uint8_t*>(configJson), n));
      return true;
    }
  }
  if (configCadence_.configDue(now)) {
    configCadence_.onConfigEmitted(now);
    emitConfig();
    return true;
  }
  return false;
}

void StatusEmitter::checkConnEdges() {
  const bool wifiConn = WiFi.isConnected();
  const bool ethUp = eth::up();
  if (!haveLastConnState_) {
    haveLastConnState_ = true;
  } else if (wifiConn != lastWifiConnected_ || ethUp != lastEthUp_) {
    Serial.printf("[wisp.beacon] connectivity edge wifi=%d eth=%d\n",
                  (int)wifiConn, (int)ethUp);
    triggerOnChange();
  }
  lastWifiConnected_ = wifiConn;
  lastEthUp_ = ethUp;
}

void StatusEmitter::emitStatus() {
  if (!mesh_) return;

#ifdef LAMP_DEBUG
  Serial.printf("[wispheap] free=%u largest=%u\n",
                (unsigned)esp_get_free_heap_size(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif

  const bool wifiConn   = WiFi.isConnected();
  const bool sourceLive = controller_ && controller_->sourceLive();
  const bool wifiChannelMismatch = wifi_ && wifi_->channelMismatch();
  const int  wifiApChannel = wifi_ ? wifi_->apChannel() : 0;
  char ethIp[16];
  eth::formatIp(ethIp, sizeof(ethIp));

  char paletteIdPrefix[kPaletteIdPrefixLen + 1] = {0};
  if (palette_) {
    const size_t n = palette_->copyPaletteIdPrefix(paletteIdPrefix,
                                                   kPaletteIdPrefixLen);
    paletteIdPrefix[n] = '\0';
  }

  const uint32_t lastSeenMs = millis();

  // manualPalette excluded: emitted separately via MSG_WISP_PALETTE (buildWispPalette).
  const char* sourceName = "off";
  const char* oscRole = nullptr;
  if (config_) {
    switch (config_->sourceMode()) {
      case WispSourceMode::Off:    sourceName = "off";    break;
      case WispSourceMode::Manual: sourceName = "manual"; break;
      case WispSourceMode::Osc:    sourceName = "osc";    break;
    }
    oscRole = oscRoleName(config_->oscRole());
  }

  uint8_t offR = 0, offG = 0, offB = 0, offW = 0;
  bool hasOffColor = false;
  const char* wispName = "";
  bool hasPassword = false;
  if (config_) {
    const auto off = config_->offColor();
    offR = off.r; offG = off.g; offB = off.b; offW = off.w;
    hasOffColor = true;
    wispName = config_->name().c_str();
    hasPassword = config_->password().length() > 0;
  }
  const WispStatusFields fields{
      wifiConn, sourceLive, paletteIdPrefix, lastSeenMs,
      sourceName, oscRole, offR, offG, offB, offW, hasOffColor,
      wispName, hasPassword, wifiChannelMismatch, wifiApChannel, ethIp };
  const WispConfigFields configCopy = configFields();

  char jsonBuf[kStatusJsonBufLen];
  const size_t jsonLen = buildWispStatusJson(
      fields, &configCopy, jsonBuf, sizeof(jsonBuf),
      lamp_protocol::CONTROL_MAX_PAYLOAD);

  // Emit palette first: a wispStatus build failure must not suppress it.
  emitPalette();

  if (jsonLen == 0) {
    Serial.println("[wisp.beacon] wispStatus JSON build failed");
    return;
  }
  broadcastControlOp(jsonBuf, jsonLen, "wispStatus");
}

WispConfigFields StatusEmitter::configFields() const {
  WispConfigFields c;
  if (!config_) return c;
  c.ledType = lampos::led::byteOrderToString(config_->ledFormat());
  c.pixelCount = config_->pixelCount();
  c.brightness = config_->brightness();
  c.driftIntervalMs = config_->driftIntervalMs();
  c.driftFadePct = config_->driftFadePct();
  c.shuffleSeed = config_->shuffleSeed();
  c.opSeq = config_->opSeq();
  return c;
}

size_t StatusEmitter::buildConfig(char* out, size_t outCap) const {
  return buildWispConfigJson(configFields(), out, outCap);
}

void StatusEmitter::emitConfig() {
  if (!mesh_) return;
  char jsonBuf[kConfigJsonBufLen];
  const size_t jsonLen = buildConfig(jsonBuf, sizeof(jsonBuf));
  if (jsonLen == 0) {
    Serial.println("[wisp.beacon] wispConfig JSON build failed");
    return;
  }
  broadcastControlOp(jsonBuf, jsonLen, "wispConfig");
}

void StatusEmitter::broadcastControlOp(const char* json, size_t jsonLen,
                                       const char* what) {
  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);
  // SeqSource mux: concurrent HELLO and control-op emits must not share a seq.
  uint8_t frame[lamp_protocol::CONTROL_MAX_SIZE];
  size_t frameLen = 0;
  uint16_t seq = 0;
  WISP_SEQ_PORTMUX_ENTER(&seq_->mux);
  seq = seq_->next();
  frameLen = lamp_protocol::buildControlOp(
      frame, sizeof(frame), seq,
      kBroadcastMac, srcMac,
      reinterpret_cast<const uint8_t*>(json), jsonLen);
  WISP_SEQ_PORTMUX_EXIT(&seq_->mux);
  if (!frameLen) {
    Serial.printf("[wisp.beacon] buildControlOp(%s) failed\n", what);
    return;
  }
  mesh_->broadcast(frame, frameLen);
}

void StatusEmitter::emitPalette() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  uint8_t rgb[lamp_protocol::kMaxWispPaletteColors * 3];
  uint8_t w[lamp_protocol::kMaxWispPaletteColors];
  size_t count = 0;
  if (config_) {
    count = config_->copyManualPalette(rgb, w,
                                       lamp_protocol::kMaxWispPaletteColors);
  }

  uint8_t frame[lamp_protocol::WISP_PALETTE_MAX_SIZE];
  size_t frameLen = 0;
  uint16_t seq = 0;
  WISP_SEQ_PORTMUX_ENTER(&seq_->mux);
  seq = seq_->next();
  frameLen = lamp_protocol::buildWispPalette(
      frame, sizeof(frame), seq, srcMac,
      count > 0 ? rgb : nullptr, count > 0 ? w : nullptr, count);
  WISP_SEQ_PORTMUX_EXIT(&seq_->mux);
  if (!frameLen) {
    Serial.println("[wisp.beacon] buildWispPalette failed");
    return;
  }
  mesh_->broadcast(frame, frameLen);
}

}  // namespace wisp
