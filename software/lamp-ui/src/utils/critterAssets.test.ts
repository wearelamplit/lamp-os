import { existsSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'

// Guards the SPIFFS pipeline: a renamed/dropped critter file 404s only the
// lamps whose critterIndexFor() hash lands on it, easy to miss by hand.
const critterDir = join(dirname(fileURLToPath(import.meta.url)), '../../../lamp-os/data/critters')

describe('critter SPIFFS assets', () => {
  const names = [...Array.from({ length: 16 }, (_, i) => `critter-${i + 1}.svg.gz`), 'critter-stray.svg.gz']

  it.each(names)('%s exists', (name) => {
    expect(existsSync(join(critterDir, name))).toBe(true)
  })
})
