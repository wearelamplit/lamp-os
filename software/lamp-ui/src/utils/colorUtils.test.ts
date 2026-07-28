import { describe, expect, it } from 'vitest'
import { blendedIdentity, parseHexww } from './colorUtils'

describe('blendedIdentity', () => {
  it('returns the single stop unchanged', () => {
    expect(blendedIdentity(['#0A141EC8'])).toBe('#0A141EC8')
  })

  it('passes the warm-white channel through for identical stops', () => {
    // Mirrors test_white_channel_passthrough in
    // software/lamp-os/test/test_blended_identity/blended_identity.cpp.
    expect(blendedIdentity(['#0A141EC8', '#0A141EC8'])).toBe('#0A141EC8')
  })

  it('leans toward the dominant stop, not a gamma average', () => {
    // Mirrors test_two_red_one_blue_leans_red: two red stops + one blue.
    const out = blendedIdentity(['#FF000000', '#FF000000', '#0000FF00'])
    const { red, blue, green } = parseHexww(out!)
    expect(red).toBeGreaterThan(blue)
    expect(green).toBe(0)
  })

  it('guards a complementary pair away from grey', () => {
    // Mirrors test_complementary_guard_avoids_grey: pure red + pure cyan
    // would average to neutral grey; the guard restores chroma along the
    // first stop's (red) hue instead.
    const out = blendedIdentity(['#FF000000', '#00FFFF00'])
    const { red, green, blue } = parseHexww(out!)
    expect(red).toBeGreaterThan(green)
    expect(red).toBeGreaterThan(blue)
  })

  it('returns undefined for no stops', () => {
    expect(blendedIdentity([])).toBeUndefined()
  })
})
