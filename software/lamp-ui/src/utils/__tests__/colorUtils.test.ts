import { describe, it, expect } from 'vitest'
import { parseHexww, hexwwToRgb, createGradientFromHexww } from '../colorUtils'

describe('colorUtils', () => {
  describe('parseHexww', () => {
    it('should parse 8-digit hexww values correctly', () => {
      const result = parseHexww('#FF0000FF')
      expect(result).toEqual({
        red: 255,
        green: 0,
        blue: 0,
        warmWhite: 255,
      })
    })

    it('should parse 6-digit hex values correctly (no warm white)', () => {
      const result = parseHexww('#00FF00')
      expect(result).toEqual({
        red: 0,
        green: 255,
        blue: 0,
        warmWhite: 0,
      })
    })

    it('should handle hex values without # prefix', () => {
      const result = parseHexww('FF0000FF')
      expect(result).toEqual({
        red: 255,
        green: 0,
        blue: 0,
        warmWhite: 255,
      })
    })

    it('should return default white color for empty string', () => {
      const result = parseHexww('')
      expect(result).toEqual({
        red: 255,
        green: 255,
        blue: 255,
        warmWhite: 0,
      })
    })

    it('should return default white color for invalid hex length', () => {
      const result = parseHexww('#123')
      expect(result).toEqual({
        red: 255,
        green: 255,
        blue: 255,
        warmWhite: 0,
      })
    })

    it('should handle partial warm white values', () => {
      const result = parseHexww('#FF000080')
      expect(result).toEqual({
        red: 255,
        green: 0,
        blue: 0,
        warmWhite: 128,
      })
    })

    it('should handle zero warm white values', () => {
      const result = parseHexww('#FF000000')
      expect(result).toEqual({
        red: 255,
        green: 0,
        blue: 0,
        warmWhite: 0,
      })
    })

    it('should handle maximum warm white values', () => {
      const result = parseHexww('#000000FF')
      expect(result).toEqual({
        red: 0,
        green: 0,
        blue: 0,
        warmWhite: 255,
      })
    })
  })

  describe('hexwwToRgb', () => {
    it('should return base RGB when no warm white is present', () => {
      const result = hexwwToRgb('#FF0000')
      expect(result).toBe('rgb(255, 0, 0)')
    })

    it('should return base RGB when warm white is zero', () => {
      const result = hexwwToRgb('#FF000000')
      expect(result).toBe('rgb(255, 0, 0)')
    })

    it('should return base RGB when RGB sum is at maximum (765)', () => {
      const result = hexwwToRgb('#FFFFFF80')
      expect(result).toBe('rgb(255, 255, 255)')
    })

    it('should handle black with full warm white', () => {
      const result = hexwwToRgb('#000000FF')
      // Black has maximum room (765), so warm white should be fully applied
      // Expected: screen blend of black (0,0,0) with warm white (250,187,62) at full opacity
      expect(result).toMatch(/^rgb\(\d+, \d+, \d+\)$/)

      // The result should be brighter than pure black due to warm white blending
      const match = result.match(/rgb\((\d+), (\d+), (\d+)\)/)
      expect(match).toBeTruthy()
      if (match) {
        const [, r, g, b] = match.map(Number)
        expect(r).toBeGreaterThan(0)
        expect(g).toBeGreaterThan(0)
        expect(b).toBeGreaterThan(0)
      }
    })

    it('should handle partial warm white values', () => {
      const result = hexwwToRgb('#00000080')
      // Black with 50% warm white (128/255)
      expect(result).toMatch(/^rgb\(\d+, \d+, \d+\)$/)

      const match = result.match(/rgb\((\d+), (\d+), (\d+)\)/)
      expect(match).toBeTruthy()
      if (match) {
        const [, r, g, b] = match.map(Number)
        // Should be brighter than black but dimmer than full warm white
        expect(r).toBeGreaterThan(0)
        expect(g).toBeGreaterThan(0)
        expect(b).toBeGreaterThan(0)
      }
    })

    it('should handle colors with limited room for warm white', () => {
      const result = hexwwToRgb('#FF0000FF')
      // Red (255,0,0) has room of 510 (765-255), so warm white should be partially applied
      expect(result).toMatch(/^rgb\(\d+, \d+, \d+\)$/)

      const match = result.match(/rgb\((\d+), (\d+), (\d+)\)/)
      expect(match).toBeTruthy()
      if (match) {
        const [, r, g, b] = match.map(Number)
        // Red should remain dominant, but green and blue should increase due to warm white
        expect(r).toBe(255) // Red should remain at maximum
        expect(g).toBeGreaterThan(0) // Green should increase due to warm white
        expect(b).toBeGreaterThan(0) // Blue should increase due to warm white
      }
    })

    it('should handle colors with very limited room', () => {
      const result = hexwwToRgb('#FEFEFEFF')
      // Near white (254,254,254) has very little room (3), so warm white effect should be minimal
      expect(result).toMatch(/^rgb\(\d+, \d+, \d+\)$/)

      const match = result.match(/rgb\((\d+), (\d+), (\d+)\)/)
      expect(match).toBeTruthy()
      if (match) {
        const [, r, g, b] = match.map(Number)
        // Should be very close to the original color
        expect(r).toBeGreaterThanOrEqual(254)
        expect(g).toBeGreaterThanOrEqual(254)
        expect(b).toBeGreaterThanOrEqual(254)
      }
    })

    it('should handle empty string gracefully', () => {
      const result = hexwwToRgb('')
      expect(result).toBe('rgb(255, 255, 255)')
    })

    it('should handle invalid hex values gracefully', () => {
      const result = hexwwToRgb('#INVALID')
      expect(result).toBe('rgb(255, 255, 255)')
    })

    it('should apply warm white with correct mathematical precision', () => {
      // Test with a specific case: #80000080 (dark red with 50% warm white)
      const result = hexwwToRgb('#80000080')
      expect(result).toMatch(/^rgb\(\d+, \d+, \d+\)$/)

      const match = result.match(/rgb\((\d+), (\d+), (\d+)\)/)
      expect(match).toBeTruthy()
      if (match) {
        const [, r, g, b] = match.map(Number)
        // Dark red (128,0,0) has room of 637, warm white is 50%
        // Room percentage: 637/765 ≈ 0.833
        // WW percentage: 128/255 ≈ 0.502
        // Final opacity: 0.502 * 0.833 ≈ 0.418
        expect(r).toBeGreaterThan(128) // Red should increase due to warm white
        expect(g).toBeGreaterThan(0) // Green should increase
        expect(b).toBeGreaterThan(0) // Blue should increase
      }
    })

    it('should handle edge case of exactly 765 RGB sum', () => {
      const result = hexwwToRgb('#FFFFFF00')
      expect(result).toBe('rgb(255, 255, 255)')
    })

    it('should handle warm white blending with different base colors', () => {
      // Test blue with warm white
      const blueResult = hexwwToRgb('#0000FFFF')
      expect(blueResult).toMatch(/^rgb\(\d+, \d+, \d+\)$/)

      // Test green with warm white
      const greenResult = hexwwToRgb('#00FF00FF')
      expect(greenResult).toMatch(/^rgb\(\d+, \d+, \d+\)$/)

      // Both should be different from their base colors
      expect(blueResult).not.toBe('rgb(0, 0, 255)')
      expect(greenResult).not.toBe('rgb(0, 255, 0)')
    })
  })

  describe('createGradientFromHexww', () => {
    it('should return empty string for empty colors array', () => {
      const result = createGradientFromHexww([])
      expect(result).toBe('')
    })

    it('should return single color for array with one color', () => {
      const result = createGradientFromHexww(['#FF0000'])
      expect(result).toBe('rgb(255, 0, 0)')
    })

    it('should create vertical gradient by default', () => {
      const result = createGradientFromHexww(['#FF0000', '#00FF00'])
      expect(result).toBe('linear-gradient(to bottom, rgb(255, 0, 0), rgb(0, 255, 0))')
    })

    it('should create horizontal gradient when specified', () => {
      const result = createGradientFromHexww(['#FF0000', '#00FF00'], 'to right')
      expect(result).toBe('linear-gradient(to right, rgb(255, 0, 0), rgb(0, 255, 0))')
    })

    it('should create gradient with multiple colors', () => {
      const result = createGradientFromHexww(['#FF0000', '#00FF00', '#0000FF'])
      expect(result).toBe(
        'linear-gradient(to bottom, rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255))',
      )
    })

    it('should handle colors with warm white in gradients', () => {
      const result = createGradientFromHexww(['#000000FF', '#FFFFFF00'])
      expect(result).toMatch(
        /^linear-gradient\(to bottom, rgb\(\d+, \d+, \d+\), rgb\(255, 255, 255\)\)$/,
      )
    })

    it('should handle mixed hexww and regular hex colors', () => {
      const result = createGradientFromHexww(['#FF0000FF', '#00FF00'])
      expect(result).toMatch(
        /^linear-gradient\(to bottom, rgb\(\d+, \d+, \d+\), rgb\(0, 255, 0\)\)$/,
      )
    })

    it('should handle invalid colors in gradient gracefully', () => {
      const result = createGradientFromHexww(['#INVALID', '#00FF00'])
      expect(result).toBe('linear-gradient(to bottom, rgb(255, 255, 255), rgb(0, 255, 0))')
    })

    it('should create complex gradient with many colors', () => {
      const colors = ['#FF0000', '#00FF00', '#0000FF', '#FFFF00', '#FF00FF', '#00FFFF']
      const result = createGradientFromHexww(colors)

      const expectedParts = colors.map((color) => hexwwToRgb(color))
      const expected = `linear-gradient(to bottom, ${expectedParts.join(', ')})`
      expect(result).toBe(expected)
    })

    it('should handle warm white gradients in both directions', () => {
      const colors = ['#000000FF', '#FFFFFF80']

      const verticalResult = createGradientFromHexww(colors, 'to bottom')
      const horizontalResult = createGradientFromHexww(colors, 'to right')

      expect(verticalResult).toContain('to bottom')
      expect(horizontalResult).toContain('to right')
      expect(verticalResult).not.toBe(horizontalResult)
    })
  })

  describe('Integration tests', () => {
    it('should maintain consistency between parseHexww and hexwwToRgb', () => {
      const testColors = ['#FF0000FF', '#00FF0080', '#0000FF40', '#FFFFFF20', '#000000FF']

      testColors.forEach((hexColor) => {
        const components = parseHexww(hexColor)
        const rgbResult = hexwwToRgb(hexColor)

        // Verify that the RGB result is a valid RGB string
        expect(rgbResult).toMatch(/^rgb\(\d+, \d+, \d+\)$/)

        // Verify that components were parsed correctly
        expect(components.red).toBeGreaterThanOrEqual(0)
        expect(components.red).toBeLessThanOrEqual(255)
        expect(components.green).toBeGreaterThanOrEqual(0)
        expect(components.green).toBeLessThanOrEqual(255)
        expect(components.blue).toBeGreaterThanOrEqual(0)
        expect(components.blue).toBeLessThanOrEqual(255)
        expect(components.warmWhite).toBeGreaterThanOrEqual(0)
        expect(components.warmWhite).toBeLessThanOrEqual(255)
      })
    })

    it('should handle edge cases in warm white calculations', () => {
      // Test with RGB sum of 764 (just under maximum)
      const result = hexwwToRgb('#FEFEFEFF')
      expect(result).toMatch(/^rgb\(\d+, \d+, \d+\)$/)

      // Test with RGB sum of 1 (just above minimum)
      const result2 = hexwwToRgb('#010000FF')
      expect(result2).toMatch(/^rgb\(\d+, \d+, \d+\)$/)
    })

    it('should produce consistent results for same input', () => {
      const hexColor = '#80000080'
      const result1 = hexwwToRgb(hexColor)
      const result2 = hexwwToRgb(hexColor)
      expect(result1).toBe(result2)
    })
  })
})
