/**
 * Color utility functions for handling hexww color format
 * and converting RGB+WW to final RGB colors
 */

export interface ColorComponents {
  red: number
  green: number
  blue: number
  warmWhite: number
}

/**
 * Parse a hexww color string and extract RGB and WW components
 */
export function parseHexww(hexValue: string): ColorComponents {
  if (!hexValue) {
    return { red: 255, green: 255, blue: 255, warmWhite: 0 }
  }

  const hex = hexValue.replace('#', '')
  if (hex.length >= 6) {
    const red = parseInt(hex.substring(0, 2), 16)
    const green = parseInt(hex.substring(2, 4), 16)
    const blue = parseInt(hex.substring(4, 6), 16)
    const warmWhite = hex.length >= 8 ? parseInt(hex.substring(6, 8), 16) : 0

    // Check if any of the parsed values are NaN (invalid hex)
    if (isNaN(red) || isNaN(green) || isNaN(blue) || isNaN(warmWhite)) {
      return { red: 255, green: 255, blue: 255, warmWhite: 0 }
    }

    return { red, green, blue, warmWhite }
  }

  return { red: 255, green: 255, blue: 255, warmWhite: 0 }
}

/**
 * Convert RGB+WW components to final RGB color
 * This simulates how the ColorPreview component renders colors
 */
export function hexwwToRgb(hexValue: string): string {
  const components = parseHexww(hexValue)
  const { red, green, blue, warmWhite } = components

  // Calculate RGB sum (0-765)
  const rgbSum = red + green + blue

  // Calculate available room (0-765)
  const availableRoom = 765 - rgbSum

  // If no room available or no WW, return base RGB
  if (availableRoom <= 0 || warmWhite === 0) {
    return `rgb(${red}, ${green}, ${blue})`
  }

  // Calculate room percentage (0-1)
  const roomPercentage = availableRoom / 765

  // Calculate WW percentage (0-1)
  const wwPercentage = warmWhite / 255

  // Final opacity is WW percentage of available room percentage
  const wwOpacity = wwPercentage * roomPercentage

  // Warm white color (warm orange/yellow tone)
  const warmWhiteColor = { r: 250, g: 187, b: 62 } // #fabb3e

  // Blend RGB with warm white using screen blend mode
  // Screen blend: 1 - (1 - a) * (1 - b)
  const blendedRed = Math.round(
    255 * (1 - (1 - red / 255) * (1 - (warmWhiteColor.r / 255) * wwOpacity)),
  )
  const blendedGreen = Math.round(
    255 * (1 - (1 - green / 255) * (1 - (warmWhiteColor.g / 255) * wwOpacity)),
  )
  const blendedBlue = Math.round(
    255 * (1 - (1 - blue / 255) * (1 - (warmWhiteColor.b / 255) * wwOpacity)),
  )

  return `rgb(${blendedRed}, ${blendedGreen}, ${blendedBlue})`
}

/**
 * Create a CSS gradient string from an array of hexww colors
 */
export function createGradientFromHexww(
  colors: string[],
  direction: 'to bottom' | 'to right' = 'to bottom',
): string {
  if (colors.length === 0) return ''
  if (colors.length === 1) return hexwwToRgb(colors[0])

  const rgbColors = colors.map((color) => hexwwToRgb(color))
  return `linear-gradient(${direction}, ${rgbColors.join(', ')})`
}

