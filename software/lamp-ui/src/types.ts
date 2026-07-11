// Mirrors the firmware's whole-document config contract
// (Config::asJsonDocument; password is stripped server-side on GET and
// re-injected on PUT, so it never appears here).

export interface LampConfig {
  name: string
  // Write-only: GET strips it, so it's absent on load; only set on PUT when
  // the user types a new one (empty = firmware keeps the existing password).
  password?: string
  brightness: number
  setup: boolean
  advancedEnabled: boolean
  devMode: boolean
  webappEnabled: boolean
  socialMode: number
}

export interface BaseConfig {
  px: number
  ac: number
  bpp: number
  byteOrder: string
  colors: string[]
  knockout: number[] // dense, one 0-100 entry per base pixel (100 = no knockout)
}

export interface ShadeConfig {
  px: number
  bpp: number
  byteOrder: string
  colors: string[]
}

// target: 1 = Shade, 2 = Base, 3 = Both.
// The index signature preserves params the UI doesn't render (durationMin,
// pulseSpeed, …) across the round-trip — they stay BLE-only but untouched.
export interface Expression {
  type: string
  enabled: boolean
  intervalMin: number
  intervalMax: number
  target: number
  colors: string[]
  [key: string]: unknown
}

export interface HomeMode {
  ssid: string
  brightness: number
  enabled: boolean
}

export interface Config {
  lamp: LampConfig
  base: BaseConfig
  shade: ShadeConfig
  expressions: Expression[]
  homeMode: HomeMode
}

// Expression catalog served by the firmware at GET /api/expressions. The web
// UI renders a subset of these archetypes and ignores the rest (zones, invert,
// requiresZoning params); un-rendered instance keys survive save via merge.

// Resolves against a surface's pixel count: literal, whole strip, or capped.
export type Bound = number | { rel: 'pixels'; cap?: number }

export interface CatalogColors {
  max: number
  label?: string
  help?: string
  inheritsSurface?: boolean
}

export interface CatalogRange {
  min: number
  max: number
  step: number
  unit?: string
  default: [number, number]
  label?: string
  minKey?: string
  maxKey?: string
}

export interface CatalogEnumOption {
  value: number
  label: string
  zoning?: boolean
}

export interface CatalogParam {
  key: string
  type: 'int' | 'enum'
  label: string
  min?: number
  max?: Bound
  step?: number
  default?: Bound
  unit?: string
  invert?: boolean
  leftLabel?: string
  rightLabel?: string
  requiresZoning?: boolean
  options?: CatalogEnumOption[]
}

export interface ExpressionDescriptor {
  id: string
  name: string
  continuous: boolean
  pausesWispOverride?: boolean
  colors: CatalogColors
  interval?: CatalogRange
  duration?: CatalogRange
  zone?: { optional?: boolean }
  excludeTargets?: string[]
  params?: CatalogParam[]
}
