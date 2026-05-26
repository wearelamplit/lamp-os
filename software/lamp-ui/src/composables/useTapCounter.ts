import { ref } from 'vue'

/**
 * Counts taps in a sliding time window and fires a callback when
 * `count` taps land within `windowMs` of each other. Designed for
 * "secret tap to unlock" gestures.
 */
export function useTapCounter(
  count: number,
  windowMs: number,
  onTriggered: () => void,
) {
  const taps = ref<number[]>([])

  function recordTap() {
    const now = Date.now()
    taps.value = [...taps.value.filter((t) => now - t < windowMs), now]
    if (taps.value.length >= count) {
      taps.value = []
      onTriggered()
    }
  }

  return { recordTap }
}
