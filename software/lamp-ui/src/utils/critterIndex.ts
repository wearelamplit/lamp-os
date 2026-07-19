// Deterministic critter index (1..16) for a lamp identity (lampId, mesh mac).
// Must match the Flutter app's derivation so the same lamp shows the same critter everywhere.
export function critterIndexFor(identity: string): number {
  let sum = 0
  const up = identity.toUpperCase()
  for (let i = 0; i < up.length; i++) sum += up.charCodeAt(i)
  return (sum % 16) + 1
}
