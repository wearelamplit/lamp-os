// Rolls a millisecond value into its two most-significant units (h/m/s/ms),
// dropping a trailing zero unit. `2h 4m`, `1m 30s`, `450ms`.
const UNITS: [number, string][] = [
  [3600000, 'h'],
  [60000, 'm'],
  [1000, 's'],
  [1, 'ms'],
]

export function formatDuration(ms: number): string {
  if (ms <= 0) return '0ms'
  const parts: string[] = []
  let remaining = ms
  for (const [size, suffix] of UNITS) {
    if (parts.length === 2) break
    const value = Math.floor(remaining / size)
    if (value === 0) {
      if (parts.length === 0) continue
      break
    }
    parts.push(`${value}${suffix}`)
    remaining -= value * size
  }
  return parts.join(' ')
}
