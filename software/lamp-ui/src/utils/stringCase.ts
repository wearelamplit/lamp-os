// Title-cases each word: first letter upper, rest lower. Lamps store names
// canonically lowercase; this is the display form. `jacko` -> `Jacko`,
// `living room` -> `Living Room`.
export function toTitleCase(s: string): string {
  return s.toLowerCase().replace(/\b\w/g, (c) => c.toUpperCase())
}
