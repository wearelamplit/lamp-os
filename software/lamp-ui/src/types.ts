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
  homeModePassword?: string
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

interface MqttSettings {
  enabled?: boolean
  brokerHost?: string
  brokerPort?: number
  username?: string
  password?: string
  topicPrefix?: string
}

export interface Settings {
  lamp?: LampSettings
  shade?: ShadeSettings
  base?: BaseSettings
  mqtt?: MqttSettings
}
