#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "config/config_types.hpp"
#include "util/color.hpp"

namespace lamp {

class DispositionStore;
struct NearbyCopy;

// Expression types with their own energy weight, in FondnessTuning::typeWeight
// order. Any other type weighs kUnknownTypeWeight.
inline constexpr const char* kEnergyTypes[] = {"glitchy", "spotty",    "pulse", "shimmer",
                                               "shifty",  "breathing", "yawn"};
inline constexpr size_t kEnergyTypeCount = sizeof(kEnergyTypes) / sizeof(kEnergyTypes[0]);
inline constexpr uint8_t kUnknownTypeWeight = 1;

// The fondness rules' feel numbers, in score units (127 = one disposition
// step), live-tunable over serial on a debug build and reset at reboot. Mode
// arrays run introvert, ambivert, extrovert.
struct FondnessTuning {
  // Per counted peer event: energy x typeWeight x the mode's sign (-1 0 +1).
  uint8_t energy = 2;
  std::array<uint8_t, kEnergyTypeCount> typeWeight = {3, 2, 2, 2, 1, 1, 0};
  // Events per peer per minute that count.
  uint8_t eventsPerMin = 4;
  // Per counted event of a type this lamp has enabled itself.
  uint8_t overlap = 2;
  // Per greet whose COLOR_INFO stop 0 sits within hueBand degrees of this
  // lamp's base or its complement.
  uint8_t colour = 16;
  uint8_t hueBand = 30;
  // Per counted meeting: familiar x familiarMode x 8 / (8 + meetings).
  uint8_t familiar = 24;
  std::array<uint8_t, 3> familiarMode = {2, 1, 1};
  // On-hours between counted meetings.
  uint8_t meetEveryH = 12;
  // Per tracked hour, toward 0, for each row unseen an hour or more.
  uint8_t absence = 8;
  // The most one peer's expression traffic moves its score per minute.
  uint8_t deltaCapPerMin = 6;
  // At most one step per peer per notchFloorMin, and notchesPerHour steps
  // across all peers.
  uint16_t notchFloorMin = 60;
  uint8_t notchesPerHour = 4;
};

// Sets FondnessTuning field `key` to `value`: a member name, `e.<type>` for a
// typeWeight entry, or `familiarMode.<mode>`. False, with `t` unchanged, for an
// unknown key or a value out of range.
bool setFondnessTuning(FondnessTuning& t, const char* key, uint32_t value);

// Whether this lamp has `type` enabled itself, the overlap rule's input.
bool hasEnabledType(const ExpressionSettings& settings, const std::string& type);

// Turns what this lamp observes of its peers into disposition drift through
// the DispositionStore: expression energy and overlap batched per peer per
// minute, colour and familiarity at a greet, absence each tracked hour. Fixed
// RAM whatever the crowd, no allocation; a full slot table drops its least
// recently heard peer's minute. Every input is Core 1 and a no-op
// while not tracking(). Rules and log lines: docs/dev/social.md.
class FondnessEngine {
 public:
  static constexpr size_t kSlots = 16;
  static constexpr size_t kNotchLog = 16;
  static constexpr uint32_t kCommitMs = 60000;
  static constexpr uint32_t kHourMs = 3600000;

  explicit FondnessEngine(DispositionStore& store) : store_(store) {}

  // Home mode's live state, read at every input. Unset reads as away.
  void setHomeSource(bool (*homeActive)()) { homeActive_ = homeActive; }
  // The near view, for a lamp whose greeting does not mark peers seen itself:
  // each minute's commit then marks every acknowledged peer in it seen. Unset
  // marks none.
  void setNearSource(const std::vector<NearbyCopy>& (*near)()) { near_ = near; }

  // The one gate: the persisted master switch on and home mode off.
  bool tracking() const;
  void setEnabled(bool on, uint64_t nowMs);

  // Call every loop pass. Holds the store's clocks while not tracking, commits
  // the minute's slots and ages rows each tracked hour.
  void tick(uint64_t nowMs);

  // A peer's announced expression of `type`; `ownType` when this lamp has the
  // type enabled itself.
  void observeExpression(const uint8_t mac[6], const char* type, bool ownType,
                         SocialMode mode, uint64_t nowMs);
  // An organic greet of `mac`: counts a meeting, and arms one colour reading.
  void observeGreeting(const uint8_t mac[6], SocialMode mode, uint64_t nowMs);
  // COLOR_INFO stop 0 from `mac` against this lamp's base stop 0. Counts only
  // as the reply to the greet before it.
  void observeColour(const uint8_t mac[6], Color peerStop0, Color ownStop0, uint64_t nowMs);
  // Restarts `mac`'s unseen clock. Pass only a peer this lamp has greeted or
  // home mode has consumed, since one still waiting for its greeting would
  // lose its reunion.
  void seen(const uint8_t mac[6], uint64_t nowMs);

  size_t slotsInUse() const;

  FondnessTuning tuning;

 private:
  struct Slot {
    std::array<uint8_t, 6> mac{};
    bool used = false;
    uint8_t events = 0;
    int16_t energy = 0;
    int16_t overlap = 0;
    uint64_t lastObsMs = 0;
  };
  struct Notch {
    std::array<uint8_t, 6> mac{};
    bool used = false;
    uint64_t atMs = 0;
  };
  Slot& slotFor_(const uint8_t mac[6], uint64_t nowMs);
  void commit_(Slot& s, uint64_t nowMs);
  void commitAll_(uint64_t nowMs);
  // Nudges the store, holding a step the notch limits refuse. `parts` are the
  // per-rule contributions for the log.
  void apply_(const uint8_t mac[6], int16_t delta, int16_t familiarity,
              const std::array<int16_t, 4>& parts, uint64_t nowMs);
  bool notchAllowed_(const uint8_t mac[6], uint64_t nowMs) const;
  void recordNotch_(const uint8_t mac[6], uint64_t nowMs);

  DispositionStore& store_;
  bool (*homeActive_)() = nullptr;
  const std::vector<NearbyCopy>& (*near_)() = nullptr;
  std::array<Slot, kSlots> slots_{};
  std::array<Notch, kNotchLog> notches_{};
  std::array<uint8_t, 6> colourFrom_{};
  bool colourArmed_ = false;
  bool held_ = false;
  bool ticked_ = false;
  uint64_t lastTickMs_ = 0;
  uint64_t lastCommitMs_ = 0;
  uint64_t trackedMs_ = 0;
};

}  // namespace lamp
