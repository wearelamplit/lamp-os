// WispController is the source-mode + Aurora orchestration brain.
//
// Owns the mode-transition, manual-palette push, Aurora palette callback,
// Aurora-liveness edge handling, and the op-result application invoked by
// MeshRouter after a CONTROL_OP dispatch. Triggers the status-ring render.
// All methods run on the loop task except onAuroraPalette, which fires from
// auroraClient.loop() (also the loop task).

#pragma once

#include "paint/current_palette.hpp"  // CurrentPalette, Palette
#include "config/wisp_op_dispatcher.hpp"  // DispatchResult

class Adafruit_NeoPixel;
class AuroraPaletteClient;

namespace wisp {

class PaintDistributor;
class WispConfig;
class ZoneSelector;
class StatusEmitter;
class ArtnetEmitter;
enum class WispSourceMode : uint8_t;

class WispController {
 public:
  WispController(CurrentPalette& palette, PaintDistributor& paint,
                 WispConfig& config, ZoneSelector& zones,
                 AuroraPaletteClient& aurora, StatusEmitter& status,
                 ArtnetEmitter& artnet, Adafruit_NeoPixel& strip)
      : palette_(palette),
        paint_(paint),
        config_(config),
        zones_(zones),
        aurora_(aurora),
        status_(status),
        artnet_(artnet),
        strip_(strip) {}

  // Idempotent: safe at boot (from NVS) and on every mode flip.
  void applySourceModeTransition(WispSourceMode mode);

  // Aurora active-palette callback. Zone latch + filter + palette push + ring.
  void onAuroraPalette(int zone, const Palette& p);

  // Apply the DispatchResult from a CONTROL_OP. MeshRouter result callback.
  void applyOpResult(DispatchResult res);

  // Loop-task edge detector: Aurora stream drop holds paint off + re-emits.
  void tickAuroraLiveness();

  // Re-init the strip from the current LED format + pixel count config.
  // Call once after config load; call again after setLedStrip op.
  void applyLedConfig();

 private:
  void pushManualPaletteToCurrent();
  void renderRing();

  CurrentPalette& palette_;
  PaintDistributor& paint_;
  WispConfig& config_;
  ZoneSelector& zones_;
  AuroraPaletteClient& aurora_;
  StatusEmitter& status_;
  ArtnetEmitter& artnet_;
  Adafruit_NeoPixel& strip_;

  bool auroraWasStreaming_ = false;
};

}  // namespace wisp
