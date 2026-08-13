import type { Bound } from '../../types'

// Resolve a catalog Bound against a surface pixel count.
export const resolveBound = (b: Bound | undefined, pixels: number, fallback = 0): number => {
  if (b == null) return fallback
  if (typeof b === 'number') return b
  return b.cap != null ? Math.min(pixels, b.cap) : pixels
}
