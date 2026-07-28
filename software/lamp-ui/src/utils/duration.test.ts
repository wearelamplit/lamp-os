import { describe, expect, it } from 'vitest'
import { formatDuration } from './duration'

describe('formatDuration', () => {
  it('zero', () => expect(formatDuration(0)).toBe('0ms'))
  it('sub-second stays in ms', () => expect(formatDuration(999)).toBe('999ms'))
  it('exact second drops trailing zero ms', () => expect(formatDuration(1000)).toBe('1s'))
  it('seconds below the minute rollover', () => expect(formatDuration(59000)).toBe('59s'))
  it('exact minute drops trailing zero s', () => expect(formatDuration(60000)).toBe('1m'))
  it('minute plus seconds', () => expect(formatDuration(61000)).toBe('1m 1s'))
  it('exact hour drops trailing zero m', () => expect(formatDuration(3600000)).toBe('1h'))
  it('hour plus minutes', () => expect(formatDuration(3660000)).toBe('1h 1m'))
  it('caps at two units, dropping the third', () => expect(formatDuration(3661000)).toBe('1h 1m'))
  it('rolls minutes into hours mid-range', () => expect(formatDuration(124 * 60000)).toBe('2h 4m'))
})
