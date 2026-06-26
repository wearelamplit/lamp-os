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
  colorsRandomized: boolean
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
