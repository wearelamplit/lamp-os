#include "core/fondness_engine.hpp"

#include <Arduino.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "config/disposition_store.hpp"
#include "util/bd_addr.hpp"

namespace lamp {

namespace {

std::array<uint8_t, 6> macKey(const uint8_t mac[6]) {
  std::array<uint8_t, 6> k;
  std::memcpy(k.data(), mac, 6);
  return k;
}

int16_t sat16(int32_t v) {
  return static_cast<int16_t>(std::clamp<int32_t>(v, INT16_MIN, INT16_MAX));
}

int modeSign(SocialMode mode) {
  if (mode == SocialMode::Introvert) return -1;
  if (mode == SocialMode::Extrovert) return 1;
  return 0;
}

// Saturation under 25%, white-only included, has no hue worth comparing.
bool isGrey(Color c) {
  const int mx = std::max<int>(c.r, std::max<int>(c.g, c.b));
  const int mn = std::min<int>(c.r, std::min<int>(c.g, c.b));
  return mx == 0 || (mx - mn) * 4 < mx;
}

#ifdef LAMP_DEBUG
const char* const kRuleNames[] = {"energy", "overlap", "colour", "familiar"};

void printScore(int8_t score) {
  const int hundredths = std::abs(score) * 100 / 127;
  Serial.printf("%s%d.%02d", score < 0 ? "-" : "", hundredths / 100, hundredths % 100);
}
#endif

}  // namespace

bool setFondnessTuning(FondnessTuning& t, const char* key, uint32_t value) {
  struct Field {
    const char* name;
    uint8_t* field;
    uint32_t lo;
    uint32_t hi;
  };
  const Field fields[] = {
      {"energy", &t.energy, 0, 32},
      {"eventsPerMin", &t.eventsPerMin, 0, 60},
      {"overlap", &t.overlap, 0, 64},
      {"colour", &t.colour, 0, 127},
      {"hueBand", &t.hueBand, 0, 90},
      {"familiar", &t.familiar, 0, 127},
      {"familiarMode.introvert", &t.familiarMode[0], 0, 4},
      {"familiarMode.ambivert", &t.familiarMode[1], 0, 4},
      {"familiarMode.extrovert", &t.familiarMode[2], 0, 4},
      {"meetEveryH", &t.meetEveryH, 0, 255},
      {"absence", &t.absence, 0, 127},
      {"deltaCapPerMin", &t.deltaCapPerMin, 1, 127},
      {"notchesPerHour", &t.notchesPerHour, 0, FondnessEngine::kNotchLog},
  };
  for (const Field& f : fields) {
    if (std::strcmp(key, f.name) != 0) continue;
    if (value < f.lo || value > f.hi) return false;
    *f.field = static_cast<uint8_t>(value);
    return true;
  }
  if (std::strcmp(key, "notchFloorMin") == 0) {
    if (value > 24 * 60) return false;
    t.notchFloorMin = static_cast<uint16_t>(value);
    return true;
  }
  if (std::strncmp(key, "e.", 2) == 0) {
    for (size_t i = 0; i < kEnergyTypeCount; ++i) {
      if (std::strcmp(key + 2, kEnergyTypes[i]) != 0) continue;
      if (value > 16) return false;
      t.typeWeight[i] = static_cast<uint8_t>(value);
      return true;
    }
  }
  return false;
}

bool FondnessEngine::tracking() const {
  return store_.engineEnabled() && !(homeActive_ && homeActive_());
}

void FondnessEngine::setEnabled(bool on, uint64_t nowMs) {
  store_.setEngineEnabled(on, nowMs);
}

void FondnessEngine::tick(uint64_t nowMs) {
  const bool on = tracking();
  if (!ticked_) {
    ticked_ = true;
    lastTickMs_ = lastCommitMs_ = nowMs;
  }
  const bool wasTracking = !held_;
  const uint64_t elapsed = nowMs - lastTickMs_;
  lastTickMs_ = nowMs;
  if (held_ == on) {
    held_ = !on;
    store_.holdClocks(held_, nowMs);
    if (held_) {
      for (Slot& s : slots_) s = Slot{};
      colourArmed_ = false;
    }
#ifdef LAMP_DEBUG
    if (!held_) Serial.println("[fondness] resumed");
    else if (homeActive_ && homeActive_()) Serial.println("[fondness] home: paused");
    else Serial.println("[fondness] off: paused");
#endif
  }
  if (!on) return;
  if (wasTracking) trackedMs_ += elapsed;
  while (trackedMs_ >= kHourMs) {
    trackedMs_ -= kHourMs;
    store_.tickHour(nowMs);
    store_.decayTowardZero(tuning.absence, nowMs);
  }
  if (nowMs - lastCommitMs_ < kCommitMs) return;
  lastCommitMs_ = nowMs;
  commitAll_(nowMs);
}

void FondnessEngine::observeExpression(const uint8_t mac[6], const char* type, bool ownType,
                                       SocialMode mode, uint64_t nowMs) {
  if (!tracking()) return;
  Slot& s = slotFor_(mac, nowMs);
  s.lastObsMs = nowMs;
  if (s.events >= tuning.eventsPerMin) return;
  ++s.events;
  uint8_t weight = kUnknownTypeWeight;
  for (size_t i = 0; i < kEnergyTypeCount; ++i) {
    if (std::strcmp(type, kEnergyTypes[i]) == 0) {
      weight = tuning.typeWeight[i];
      break;
    }
  }
  s.energy = sat16(s.energy + modeSign(mode) * tuning.energy * weight);
  if (ownType) s.overlap = sat16(s.overlap + tuning.overlap);
}

void FondnessEngine::observeGreeting(const uint8_t mac[6], SocialMode mode, uint64_t nowMs) {
  if (!tracking()) return;
  store_.markSeen(mac, nowMs);
  colourFrom_ = macKey(mac);
  colourArmed_ = true;
  if (!store_.meet(mac, tuning.meetEveryH, nowMs)) return;
  const uint8_t meets = store_.peer(mac, nowMs).meets;
  const size_t modeIdx = std::min<size_t>(static_cast<size_t>(mode), 2);
  const int16_t familiarity = sat16(static_cast<int32_t>(tuning.familiar) *
                                    tuning.familiarMode[modeIdx] * 8 / (8 + meets));
#ifdef LAMP_DEBUG
  char addr[18];
  formatBdAddr(mac, addr);
  Serial.printf("[fondness] %s meet %u\n", addr, static_cast<unsigned>(meets));
#endif
  if (familiarity != 0) apply_(mac, 0, familiarity, {0, 0, 0, familiarity}, nowMs);
}

void FondnessEngine::observeColour(const uint8_t mac[6], Color peerStop0, Color ownStop0,
                                   uint64_t nowMs) {
  if (!tracking() || !colourArmed_ || colourFrom_ != macKey(mac)) return;
  colourArmed_ = false;
  if (isGrey(peerStop0) || isGrey(ownStop0)) return;
  const int diff = std::abs(static_cast<int>(colorToHue(peerStop0)) - colorToHue(ownStop0));
  const int apart = std::min(diff, 360 - diff);
  if (apart > tuning.hueBand && apart < 180 - tuning.hueBand) return;
  const int16_t colour = tuning.colour;
  if (colour != 0) apply_(mac, colour, 0, {0, 0, colour, 0}, nowMs);
}

void FondnessEngine::seen(const uint8_t mac[6], uint64_t nowMs) {
  if (tracking()) store_.markSeen(mac, nowMs);
}

bool hasEnabledType(const ExpressionSettings& settings, const std::string& type) {
  for (const ExpressionConfig& e : settings.expressions) {
    if (e.enabled && e.type == type) return true;
  }
  return false;
}

size_t FondnessEngine::slotsInUse() const {
  return static_cast<size_t>(
      std::count_if(slots_.begin(), slots_.end(), [](const Slot& s) { return s.used; }));
}

FondnessEngine::Slot& FondnessEngine::slotFor_(const uint8_t mac[6], uint64_t nowMs) {
  const auto key = macKey(mac);
  Slot* free = nullptr;
  Slot* oldest = nullptr;
  for (Slot& s : slots_) {
    if (!s.used) {
      if (!free) free = &s;
      continue;
    }
    if (s.mac == key) return s;
    if (!oldest || s.lastObsMs < oldest->lastObsMs) oldest = &s;
  }
  // A churned peer would restart its count and cap within the minute, so the
  // oldest slot's minute is dropped, never committed early.
  if (!free) free = oldest;
  *free = Slot{};
  free->used = true;
  free->mac = key;
  return *free;
}

void FondnessEngine::commit_(Slot& s, uint64_t nowMs) {
  const std::array<uint8_t, 6> mac = s.mac;
  const int16_t energy = s.energy;
  const int16_t overlap = s.overlap;
  s = Slot{};
  const int cap = tuning.deltaCapPerMin;
  const int16_t delta = static_cast<int16_t>(std::clamp<int>(energy + overlap, -cap, cap));
  if (delta != 0) apply_(mac.data(), delta, 0, {energy, overlap, 0, 0}, nowMs);
}

void FondnessEngine::commitAll_(uint64_t nowMs) {
  for (Slot& s : slots_) {
    if (s.used) commit_(s, nowMs);
  }
}

void FondnessEngine::apply_(const uint8_t mac[6], int16_t delta, int16_t familiarity,
                            const std::array<int16_t, 4>& parts, uint64_t nowMs) {
  const NudgeResult r =
      store_.nudge(mac, delta, familiarity, nowMs, notchAllowed_(mac, nowMs));
  if (r.stepped) recordNotch_(mac, nowMs);
#ifdef LAMP_DEBUG
  if (!r.stepped && r.scoreFrom == r.scoreTo) return;
  char addr[18];
  formatBdAddr(mac, addr);
  // Print::printf mallocs a line of 64 B or more, so the line goes in pieces.
  Serial.printf("[fondness] %s score ", addr);
  printScore(r.scoreFrom);
  Serial.print(" -> ");
  printScore(r.scoreTo);
  Serial.print(" (");
  bool first = true;
  size_t dominant = 0;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (parts[i] == 0) continue;
    if (std::abs(parts[i]) > std::abs(parts[dominant])) dominant = i;
    Serial.printf("%s%s %+d", first ? "" : ", ", kRuleNames[i], static_cast<int>(parts[i]));
    first = false;
  }
  Serial.printf(") disp %u\n", static_cast<unsigned>(r.to));
  if (r.stepped) {
    Serial.printf("[fondness] %s notch %u -> %u (%s)\n", addr, static_cast<unsigned>(r.from),
                  static_cast<unsigned>(r.to), kRuleNames[dominant]);
  }
#else
  (void)parts;
#endif
}

bool FondnessEngine::notchAllowed_(const uint8_t mac[6], uint64_t nowMs) const {
  const auto key = macKey(mac);
  const uint64_t floorMs = static_cast<uint64_t>(tuning.notchFloorMin) * 60000;
  size_t inHour = 0;
  bool full = true;
  uint64_t oldestMs = UINT64_MAX;
  for (const Notch& n : notches_) {
    if (!n.used) {
      full = false;
      continue;
    }
    const uint64_t age = nowMs - n.atMs;
    if (age < kHourMs) ++inHour;
    if (n.mac == key && age < floorMs) return false;
    oldestMs = std::min(oldestMs, n.atMs);
  }
  if (inHour >= tuning.notchesPerHour) return false;
  // A full log may have dropped this peer's last step inside the floor.
  return !(full && nowMs - oldestMs < floorMs);
}

void FondnessEngine::recordNotch_(const uint8_t mac[6], uint64_t nowMs) {
  Notch* slot = &notches_[0];
  for (Notch& n : notches_) {
    if (!n.used) {
      slot = &n;
      break;
    }
    if (n.atMs < slot->atMs) slot = &n;
  }
  slot->mac = macKey(mac);
  slot->used = true;
  slot->atMs = nowMs;
}

}  // namespace lamp
