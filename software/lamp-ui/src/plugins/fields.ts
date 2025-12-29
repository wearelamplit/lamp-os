/**
 * Fields Plugin
 *
 * Registers all field components globally for dynamic form rendering.
 * Field components are registered with kebab-case names matching the type
 * property in field definitions.
 */

import type { App } from 'vue'

// Import all field components
import BooleanField from '@/components/fields/Boolean.vue'
import TextField from '@/components/fields/Text.vue'
import NumberField from '@/components/fields/Number.vue'
import NumberSliderField from '@/components/fields/NumberSlider.vue'
import NumberRangeSliderField from '@/components/fields/NumberRangeSlider.vue'
import BrightnessSliderField from '@/components/fields/BrightnessSlider.vue'
import HiddenField from '@/components/fields/Hidden.vue'
import ColorField from '@/components/fields/Color.vue'
import ColorListField from '@/components/fields/ColorList.vue'

// Map of field type names to components
const fieldComponents = {
  // Register with kebab-case names to match field type
  'boolean-field': BooleanField,
  'text-field': TextField,
  'number-field': NumberField,
  'number-slider-field': NumberSliderField,
  'number-range-slider-field': NumberRangeSliderField,
  'brightness-slider-field': BrightnessSliderField,
  'hidden-field': HiddenField,
  'color-field': ColorField,
  'color-list-field': ColorListField,
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
 * Fields plugin installer
 */
export const FieldsPlugin = {
  install(app: App) {
    // Register each field component globally
    for (const [name, component] of Object.entries(fieldComponents)) {
      app.component(name, component)
    }
  },
}

export default FieldsPlugin

