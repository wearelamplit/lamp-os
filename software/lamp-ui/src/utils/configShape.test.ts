import { describe, expect, it } from 'vitest'
import {
  SAFE_COLOR,
  paletteFromSegments,
  pxFromSegments,
  segmentsFromPalette,
} from './configShape'

describe('SAFE_COLOR', () => {
  it('pins the W=0 brownout constant', () => {
    expect(SAFE_COLOR).toBe('#FF000000')
  })
})

describe('paletteFromSegments', () => {
  it('extracts colors from the first segment', () => {
    const segs = [
      { px: 10, colors: ['#11223300', '#44556600'] },
      { px: 5, colors: ['#99999900'] },
    ]
    expect(paletteFromSegments(segs)).toEqual(['#11223300', '#44556600'])
  })

  it('falls back when segments are missing or empty', () => {
    expect(paletteFromSegments(undefined)).toEqual([SAFE_COLOR])
    expect(paletteFromSegments([])).toEqual([SAFE_COLOR])
    expect(paletteFromSegments([{ px: 0, colors: [] }])).toEqual([SAFE_COLOR])
  })
})

describe('pxFromSegments', () => {
  it('reads px from the first segment', () => {
    expect(pxFromSegments([{ px: 35, colors: ['#00000000'] }])).toBe(35)
  })

  it('returns 0 when segments are missing or lack px', () => {
    expect(pxFromSegments(undefined)).toBe(0)
    expect(pxFromSegments([])).toBe(0)
  })
})

describe('segmentsFromPalette', () => {
  it('fans the palette to every segment', () => {
    const segs = [
      { name: 'a', px: 10, colors: ['#00000000'] },
      { name: 'b', px: 5, colors: ['#00000000'] },
    ]
    const out = segmentsFromPalette(segs, ['#ff000000', '#00ff0000'])
    expect(out.map((s) => s.colors)).toEqual([
      ['#ff000000', '#00ff0000'],
      ['#ff000000', '#00ff0000'],
    ])
    expect(out.map((s) => s.name)).toEqual(['a', 'b'])
  })

  it('applies px only to the first segment', () => {
    const segs = [
      { px: 10, colors: ['#00000000'] },
      { px: 5, colors: ['#00000000'] },
    ]
    const out = segmentsFromPalette(segs, ['#ff000000'], 20)
    expect(out[0].px).toBe(20)
    expect(out[1].px).toBe(5)
  })

  it('synthesizes a single segment when none exist', () => {
    const out = segmentsFromPalette(undefined, ['#ff000000'], 12)
    expect(out).toHaveLength(1)
    expect(out[0].px).toBe(12)
    expect(out[0].colors).toEqual(['#ff000000'])
  })

  it('leaves px untouched when not provided', () => {
    const out = segmentsFromPalette([{ px: 7, colors: [] }], ['#ff000000'])
    expect(out[0].px).toBe(7)
  })
})
