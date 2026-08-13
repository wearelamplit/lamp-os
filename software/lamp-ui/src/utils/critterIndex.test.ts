import { describe, expect, it } from 'vitest'
import { critterIndexFor } from './critterIndex'

// Vectors pinned against critter_asset_test.dart to keep app + webui in sync.
describe('critterIndexFor', () => {
  it('matches the Dart reference vectors', () => {
    expect(critterIndexFor('AA:BB:CC:DD:EE:01')).toBe(2)
    expect(critterIndexFor('11:22:33:44:55:66')).toBe(13)
  })

  it('is case-insensitive', () => {
    expect(critterIndexFor('aa:bb:cc:dd:ee:01')).toBe(2)
  })
})
