// Mirrors the firmware's whole-document config contract
// (Config::asJsonDocument; password is stripped server-side on GET and
// re-injected on PUT, so it never appears here).

export interface LampConfig {
  name: string
  // Write-only: GET strips it, so it's absent on load; only set on PUT when
  // the user types a new one (empty = firmware keeps the existing password).
  password?: string
  // Read-only on GET: signals whether a password is set without exposing it.
  hasPassword?: boolean
  // Read-only on GET: the lamp's mesh mac; the critter is derived from it.
  lampId?: string
  brightness: number
  // Global max brightness the lamp will drive (0-255); the power-saving cap.
  brightnessCeiling: number
  setup: boolean
  advancedEnabled: boolean
  webappEnabled: boolean
  socialMode: number
  // Minutes the softAP/webapp stays up on boot; 0 = never expires.
  apBootMinutes: number
}

// A lamp seen recently over BLE or the mesh, served read-only at GET /api/nearby.
export interface NearbyLamp {
  name: string
  lastSeenMs: number
  viaBle: boolean
  viaEspNow: boolean
  lampId?: string
  rssi?: number
  shade: string
  base: string
  fwVersion?: number
  otaState?: number
}

// Firmware emits base/shade colors as segments[].colors, never a flat array.
export interface Segment {
  name?: string
  px: number
  colors: string[]
}

export interface BaseConfig {
  ac: number
  segments?: Segment[]
  knockout: number[] // dense, one 0-100 entry per base pixel (100 = no knockout)
  // Firmware-owned; false = colors are fixed by the variant, hide the picker.
  colorsEditable?: boolean
  // NeoPixel channel order; absent = the variant's StripSpec default.
  byteOrder?: string
}

export interface ShadeConfig {
  segments?: Segment[]
  colorsEditable?: boolean
  byteOrder?: string
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
  // Home mode only activates when the home SSID is in range (presence-driven);
  // false = plain manual on/off.
  networkBound: boolean
  // Mutes greetings and nearby-lamp reactions while home mode is active.
  socialDisabled: boolean
  // Expression type ids turned off while home mode is active.
  disabledExpressionTypes: string[]
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
  help?: string
  requiresZoning?: boolean
  options?: CatalogEnumOption[]
}

export interface ExpressionDescriptor {
  id: string
  name: string
  colors: CatalogColors
  interval?: CatalogRange
  duration?: CatalogRange
  params?: CatalogParam[]
}
