#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lamp { namespace lioness {

using Mac = std::array<uint8_t, 6>;
inline constexpr Mac kIdle{};                 // all-zero: idle (mirror Main)

// How often a single lion advances to the next lamp in the walk.
constexpr uint32_t kStaggerPeriodMs = 6000;
// Phase gap between neighbouring lions' advance deadlines. Period/3 keeps the
// three deadlines evenly spread so no two ever fire on the same tick and a
// switch lands somewhere every ~2s.
constexpr uint32_t kStaggerPhaseMs = kStaggerPeriodMs / 3;

inline bool macIn(const Mac& m, const std::vector<Mac>& v) {
  for (const Mac& e : v) if (e == m) return true;
  return false;
}

// Three lions as a sliding window over the ordered nearby list. current[i] is
// the peer lion i shows (kIdle mirrors Main). At >=3 nearby, each lion advances
// on its own staggered deadline (phase-offset by kStaggerPhaseMs) to the next
// lamp the other two aren't showing, so the trio stays distinct, every lamp
// gets a turn, and no two lions switch on one tick. tick() returns a 3-bit
// changed-mask gating each crossfade. Below 3: 0 all idle, 1 all show it, 2 the
// pair plus an idle third.
struct LionsDirector {
  std::array<Mac, 3> current{};
  std::array<uint32_t, 3> nextMs{};
  size_t nextIndex_ = 0;
  bool seeded_ = false;

  // First lamp at or after the walk head that neither `a` nor `b` shows;
  // advances the head past it. Returns nearby[head] if all are taken (n<3).
  Mac advance(const std::vector<Mac>& nearby, const Mac& a, const Mac& b) {
    const size_t n = nearby.size();
    for (size_t step = 0; step < n; ++step) {
      const size_t idx = (nextIndex_ + step) % n;
      const Mac& cand = nearby[idx];
      if (!(cand == a) && !(cand == b)) {
        nextIndex_ = (idx + 1) % n;
        return cand;
      }
    }
    return nearby[nextIndex_ % n];
  }

  uint8_t tick(uint32_t nowMs, const std::vector<Mac>& nearby) {
    std::array<Mac, 3> want = current;
    const size_t n = nearby.size();

    if (n < 3) {
      seeded_ = false;
      if (n == 0)      want = {kIdle, kIdle, kIdle};
      else if (n == 1) want = {nearby[0], nearby[0], nearby[0]};
      else             want = {nearby[0], nearby[1], kIdle};
    } else if (!seeded_) {
      want = {nearby[0], nearby[1], nearby[2]};
      nextIndex_ = 3 % n;
      for (int i = 0; i < 3; ++i)
        nextMs[i] = nowMs + static_cast<uint32_t>(i + 1) * kStaggerPhaseMs;
      seeded_ = true;
    } else {
      for (int i = 0; i < 3; ++i) {
        const Mac& a = want[(i + 1) % 3];
        const Mac& b = want[(i + 2) % 3];
        if (!macIn(want[i], nearby)) {
          want[i] = advance(nearby, a, b);
          nextMs[i] = nowMs + kStaggerPeriodMs;
        } else if (static_cast<int32_t>(nowMs - nextMs[i]) >= 0) {
          want[i] = advance(nearby, a, b);
          nextMs[i] += kStaggerPeriodMs;   // advance from own deadline to hold the phase offset
        }
      }
    }

    uint8_t changed = 0;
    for (int i = 0; i < 3; ++i) {
      if (!(want[i] == current[i])) changed |= static_cast<uint8_t>(1u << i);
      current[i] = want[i];
    }
    return changed;
  }
};

}}  // namespace lioness, namespace lamp
