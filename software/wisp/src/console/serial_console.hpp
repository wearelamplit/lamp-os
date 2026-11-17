#pragma once

#include <Arduino.h>
#include <functional>

#include "config/wisp_op_dispatcher.hpp"  // DispatchResult
#include "osc/slip_decoder.hpp"

namespace wisp {

class PaintDistributor;
class StageBeacon;
class WifiLink;
class StatusEmitter;
class LampInventory;
class OscListener;
class WispConfig;

class SerialConsole {
public:
  // Applies a setting the console stored, as the matching wispOp would.
  using ApplyFn = std::function<void(DispatchResult)>;

  SerialConsole(PaintDistributor& paint, WispConfig& config,
                StageBeacon& stage, WifiLink& wifi, StatusEmitter& status,
                LampInventory& inventory, ApplyFn apply);

  // Read available serial bytes; SLIP frames go to the OSC sink, everything
  // else dispatches as text command lines.
  void pump();

  void setOscSink(OscListener* osc) { osc_ = osc; }

  // Print the current lamp roster to Serial.
  void dumpInventory();

private:
  void handleCommand(const String& cmd);
  static String formatVersion(uint32_t v);

  PaintDistributor& paint_;
  WispConfig& config_;
  StageBeacon& stage_;
  WifiLink& wifi_;
  StatusEmitter& status_;
  LampInventory& inventory_;
  ApplyFn apply_;

  String buf_;
  OscListener* osc_ = nullptr;
  SlipDecoder slip_;
};

}  // namespace wisp
