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

// ============================================
// Form Field Types
// ============================================

/**
 * Supported field types for the dynamic form system
 */
export type FieldType =
  | 'boolean'
  | 'text'
  | 'password'
  | 'number'
  | 'number-slider'
  | 'number-range-slider'
  | 'brightness-slider'
  | 'hidden'
  | 'color'
  | 'color-list'
  | 'slot'
  | 'group-heading'

/**
 * Field props that can be passed to individual field components
 */
export interface FieldProps {
  // Common validation
  min?: number
  max?: number
  minLength?: number
  maxLength?: number
  pattern?: string
  required?: boolean

  // Number/slider specific
  step?: number
  steps?: number
  color?: string
  prepend?: string
  append?: string

  // Text specific
  placeholder?: string
  transform?: 'lowercase' | 'uppercase' | 'none'
  autocapitalize?: boolean

  // Color list specific
  maxColors?: number
  showAddButton?: boolean

  // Select buttons (future)
  optionValues?: (string | number)[]
  optionLabels?: string[]

  // Generic disabled state
  disabled?: boolean

  // Allow additional props
  [key: string]: unknown
}

/**
 * Field definition for the form system
 */
export interface FieldDefinition {
  /** Unique field name - used as key in form values */
  name?: string

  /** Field type - determines which component to render */
  type: FieldType

  /** Display label for the field */
  label: string

  /** Help text shown below the field */
  help?: string

  /** Default value for the field */
  default?: unknown

  /** Props passed to the field component */
  props?: FieldProps

  /** Transform labels for array values (e.g., ["Min", "Max"] for range slider) */
  transform?: string[]

  /** Whether the field is optional (default: false) */
  optional?: boolean

  /** Conditional visibility function - receives current form values */
  show?: (values: FormValues) => boolean
}

/**
 * Form values object - key-value pairs of field names to values
 */
export interface FormValues {
  [key: string]: unknown
}

/**
 * Field validation result
 */
export interface FieldValidationResult {
  valid: boolean
  error?: string
}

/**
 * Field component interface - what field components should expose
 */
export interface FieldComponent {
  validate?: () => FieldValidationResult | Promise<FieldValidationResult>
  onSubmit?: () => boolean | Promise<boolean>
}

/**
 * Processed field with computed values
 */
export interface ProcessedField extends FieldDefinition {
  /** Current value of the field */
  value: unknown
  /** Whether the field has a valid value */
  hasValue: boolean
  /** Whether the field should be visible */
  visible: boolean
  /** Current error message if validation failed */
  error?: string
}
