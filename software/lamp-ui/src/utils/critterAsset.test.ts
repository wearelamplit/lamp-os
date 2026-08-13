import { describe, expect, it } from 'vitest'
import { recolorCritterSvg } from './critterAsset'

const template = `<svg><defs>
<linearGradient id="critter1Shade" x1="0%" y1="0%" x2="0%" y2="100%">
<stop offset="0%" style="stop-color:#fffdd1;stop-opacity:1" />
<stop offset="100%" style="stop-color:#fffdd1;stop-opacity:1" />
</linearGradient>
<linearGradient id="critter1Body" x1="0%" y1="0%" x2="0%" y2="100%">
<stop offset="0%" style="stop-color:#fffdd1;stop-opacity:1" />
<stop offset="100%" style="stop-color:#fffdd1;stop-opacity:1" />
</linearGradient>
</defs></svg>`

describe('recolorCritterSvg', () => {
  it('rewrites both gradients to two matching stops each', () => {
    const out = recolorCritterSvg(template, 'rgb(1, 2, 3)', 'rgb(4, 5, 6)')
    expect(out).toContain(
      '<linearGradient id="critter1Shade" x1="0%" y1="0%" x2="0%" y2="100%">' +
        '<stop offset="0%" style="stop-color:rgb(1, 2, 3);stop-opacity:1"/>' +
        '<stop offset="100%" style="stop-color:rgb(1, 2, 3);stop-opacity:1"/>' +
        '</linearGradient>',
    )
    expect(out).toContain(
      '<linearGradient id="critter1Body" x1="0%" y1="0%" x2="0%" y2="100%">' +
        '<stop offset="0%" style="stop-color:rgb(4, 5, 6);stop-opacity:1"/>' +
        '<stop offset="100%" style="stop-color:rgb(4, 5, 6);stop-opacity:1"/>' +
        '</linearGradient>',
    )
  })

  it('leaves the rest of the SVG untouched', () => {
    const out = recolorCritterSvg(template, '#111', '#222')
    expect(out.startsWith('<svg><defs>\n')).toBe(true)
    expect(out.trim().endsWith('</defs></svg>')).toBe(true)
  })
})
