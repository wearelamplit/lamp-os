import { describe, expect, it } from 'vitest'
import { toTitleCase } from './stringCase'

describe('toTitleCase', () => {
  it('single lowercase word', () => expect(toTitleCase('jacko')).toBe('Jacko'))
  it('multi word', () => expect(toTitleCase('living room')).toBe('Living Room'))
  it('already capitalized normalizes', () => expect(toTitleCase('LIVING ROOM')).toBe('Living Room'))
  it('empty stays empty', () => expect(toTitleCase('')).toBe(''))
  it('blank whitespace preserved', () => expect(toTitleCase('   ')).toBe('   '))
})
