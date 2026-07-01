#pragma once

#include <Arduino.h>
#include <functional>

namespace wisp {

class PaintDistributor;
class ArtnetEmitter;
class StageBeacon;
class WifiLink;
class StatusBeacon;
class LampInventory;
class ZoneSelector;
class WispConfig;
enum class WispSourceMode : uint8_t;

class SerialConsole {
public:
  using SourceTransitionFn = std::function<void(WispSourceMode)>;

  SerialConsole(PaintDistributor& paint, WispConfig& config,
                ArtnetEmitter& artnet, StageBeacon& stage,
                WifiLink& wifi, StatusBeacon& status,
                LampInventory& inventory, ZoneSelector& zones,
                SourceTransitionFn onSourceTransition);

  // Read available serial bytes; dispatch complete lines as commands.
  void pump();

  // Print current lamp roster + zone state to Serial.
  void dumpInventory();

private:
  void handleCommand(const String& cmd);
  static String formatVersion(uint32_t v);

  PaintDistributor& paint_;
  WispConfig& config_;
  ArtnetEmitter& artnet_;
  StageBeacon& stage_;
  WifiLink& wifi_;
  StatusBeacon& status_;
  LampInventory& inventory_;
  ZoneSelector& zones_;
  SourceTransitionFn onSourceTransition_;

  String buf_;
};

}  // namespace wisp
