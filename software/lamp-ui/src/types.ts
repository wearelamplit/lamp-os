interface KnockoutPixel {
  p: number
  b: number
}

interface LampSettings {
  name?: string
  brightness?: number
  homeMode?: boolean
  homeModeSSID?: string
  homeModeBrightness?: number
  password?: string
}

interface ShadeSettings {
  px?: number
  colors?: string[]
}

interface BaseSettings {
  px?: number
  colors?: string[]
  ac?: number
  knockout?: KnockoutPixel[]
}

export interface Settings {
  lamp?: LampSettings
  shade?: ShadeSettings
  base?: BaseSettings
}

export interface Expression {
  type: string
  enabled: boolean
  colors: string[]
  intervalMin: number
  intervalMax: number
  target: number
  durationMin?: number
  durationMax?: number
  fadeDuration?: number
  shiftDurationMin?: number
  shiftDurationMax?: number
  pulseSpeed?: number
  numStars?: number
}
