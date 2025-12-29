/**
 * Global Components Plugin
 *
 * Registers commonly used components globally for use throughout the app.
 * Includes field components for dynamic form rendering and shared UI components.
 */

import type { App } from 'vue'

// Import field components
import BooleanField from '@/components/fields/Boolean.vue'
import TextField from '@/components/fields/Text.vue'
import NumberField from '@/components/fields/Number.vue'
import NumberSliderField from '@/components/fields/NumberSlider.vue'
import NumberRangeSliderField from '@/components/fields/NumberRangeSlider.vue'
import BrightnessSliderField from '@/components/fields/BrightnessSlider.vue'
import HiddenField from '@/components/fields/Hidden.vue'
import ColorField from '@/components/fields/Color.vue'
import ColorListField from '@/components/fields/ColorList.vue'
import PasswordField from '@/components/fields/Password.vue'
import GroupHeadingField from '@/components/fields/GroupHeading.vue'

// Import shared UI components
import InfoPanel from '@/components/InfoPanel.vue'

// Map of field type names to components
const fieldComponents = {
  'boolean-field': BooleanField,
  'text-field': TextField,
  'number-field': NumberField,
  'number-slider-field': NumberSliderField,
  'number-range-slider-field': NumberRangeSliderField,
  'brightness-slider-field': BrightnessSliderField,
  'hidden-field': HiddenField,
  'color-field': ColorField,
  'color-list-field': ColorListField,
  'password-field': PasswordField,
  'group-heading-field': GroupHeadingField,
} as const

// Shared UI components
const sharedComponents = {
  'InfoPanel': InfoPanel,
} as const

export type FieldComponentName = keyof typeof fieldComponents

/**
 * Get the component name for a field type
 */
export function getFieldComponentName(type: string): string {
  return `${type}-field`
}

/**
 * Check if a field type is supported
 */
export function isFieldTypeSupported(type: string): boolean {
  const componentName = getFieldComponentName(type)
  return componentName in fieldComponents
}

/**
 * Global Components plugin installer
 */
export const GlobalComponentsPlugin = {
  install(app: App) {
    // Register field components globally
    for (const [name, component] of Object.entries(fieldComponents)) {
      app.component(name, component)
    }

    // Register shared UI components globally
    for (const [name, component] of Object.entries(sharedComponents)) {
      app.component(name, component)
    }
  },
}

export default GlobalComponentsPlugin

