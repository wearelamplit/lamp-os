<!-- eslint-disable vue/multi-word-component-names -->
<script setup lang="ts">
import { ref, computed, watch, useSlots } from 'vue'
import Field from '@/components/fields/Field.vue'
import type { FieldDefinition, FormValues, FieldValidationResult, FieldComponent } from '@/types'
import { getFieldComponentName, isFieldTypeSupported } from '@/plugins/globalComponents'

interface Props {
  fields: FieldDefinition[]
  modelValue?: FormValues
  showButton?: boolean
  buttonText?: string
  disabled?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  modelValue: () => ({}),
  showButton: true,
  buttonText: 'Submit',
  disabled: false,
})

// Meta values storage for field metadata (e.g., activeColor index)
type MetaValues = Record<string, unknown>

const emit = defineEmits<{
  'update:modelValue': [value: FormValues, meta: MetaValues]
  submit: [values: FormValues, meta: MetaValues]
  meta: [fieldName: string, value: unknown]
}>()

const slots = useSlots()

// Field component refs for validation
const fieldRefs = ref<Record<string, FieldComponent>>({})

// Field errors
const fieldErrors = ref<Record<string, string>>({})

// Meta values storage for field metadata (e.g., activeColor index)
const metaValues = ref<MetaValues>({})

// Whether the form is currently submitting
const isSubmitting = ref(false)

/**
 * Transform input values from external format to internal format
 * For fields with transform (e.g., ['Min', 'Max']), combines:
 *   { intervalMin: 5, intervalMax: 15 } → { interval: [5, 15] }
 */
const transformInputValues = (values: FormValues): FormValues => {
  const transformed = { ...values }

  for (const field of props.fields) {
    if (!field.name) continue
    if (field.transform && Array.isArray(field.transform) && field.transform.length > 0) {
      const arrayValue: unknown[] = []
      let hasAnyValue = false

      for (const suffix of field.transform) {
        const key = `${field.name}${suffix}`
        if (key in transformed) {
          arrayValue.push(transformed[key])
          delete transformed[key]
          hasAnyValue = true
        } else {
          arrayValue.push(undefined)
        }
      }

      if (hasAnyValue) {
        transformed[field.name] = arrayValue
      }
    }
  }

  return transformed
}

/**
 * Transform output values from internal format to external format
 * For fields with transform (e.g., ['Min', 'Max']), splits:
 *   { interval: [5, 15] } → { intervalMin: 5, intervalMax: 15 }
 */
const transformOutputValues = (values: FormValues): FormValues => {
  const transformed = { ...values }

  for (const field of props.fields) {
    if (!field.name) continue
    if (field.transform && Array.isArray(field.transform) && field.transform.length > 0) {
      const arrayValue = transformed[field.name]

      if (Array.isArray(arrayValue)) {
        // Remove the array field
        delete transformed[field.name]

        // Add the split values
        const transformKeys = field.transform as string[]
        for (let i = 0; i < transformKeys.length; i++) {
          const suffix: string = transformKeys[i]
          const key = `${field.name}${suffix}`
          transformed[key] = arrayValue[i]
        }
      }
    }
  }

  return transformed
}

// Internal form values (transformed from input)
const formValues = ref<FormValues>(transformInputValues({ ...props.modelValue }))

/**
 * Determine the value for a field
 * Priority: modelValue > default > undefined
 */
const getFieldValue = (field: FieldDefinition): unknown => {
  if (!field.name) return undefined
  // If value exists in formValues, use it
  if (field.name in formValues.value && formValues.value[field.name] !== undefined) {
    return formValues.value[field.name]
  }
  // Fall back to default
  if (field.default !== undefined) {
    return field.default
  }
  return undefined
}

/**
 * Check if a field has a valid value (not undefined or null)
 */
const hasValidValue = (field: FieldDefinition): boolean => {
  const value = getFieldValue(field)
  return value !== undefined && value !== null
}

/**
 * Check if a field should be visible
 * - Must have a valid value (unless optional)
 * - Must pass show condition if defined
 */
const isFieldVisible = (field: FieldDefinition): boolean => {
  // Check show condition
  if (field.show && typeof field.show === 'function') {
    if (!field.show(formValues.value)) {
      return false
    }
  }

  // If field is optional, it can be shown without a value
  if (field.optional) {
    return true
  }

  // Must have a valid value
  return hasValidValue(field)
}

/**
 * Check if a slot is empty (has no content)
 */
const isSlotEmpty = (slotName: string): boolean => {
  const slot = slots[slotName]
  if (!slot) return true

  const vNodes = slot()
  if (!vNodes || vNodes.length === 0) return true

  // Check if all vnodes are empty (comments, empty text, etc.)
  return vNodes.every((vNode) => {
    if (vNode.type === Comment) return true
    if (typeof vNode.type === 'symbol') return true // Fragment, etc.
    if (typeof vNode.children === 'string' && !vNode.children.trim()) return true
    return false
  })
}

/**
 * Computed list of visible fields
 */
const visibleFields = computed(() => {
  return props.fields.filter((field) => {
    // Handle slot type
    if (field.type === 'slot') {
      return !isSlotEmpty(field.name || '')
    }

    // Group headings are always visible (no value required)
    if (field.type === 'group-heading') {
      // Check show condition if defined
      if (field.show && typeof field.show === 'function') {
        return field.show(formValues.value)
      }
      return true
    }

    // Handle regular fields
    return isFieldVisible(field)
  })
})

/**
 * Set default values for all fields
 */
const setDefaultValues = () => {
  props.fields.forEach((field) => {
    if (field.type === 'slot' || !field.name) return

    if (!(field.name in formValues.value) && field.default !== undefined) {
      formValues.value[field.name] = field.default
    }
  })
}

/**
 * Get the component name for a field type
 */
const getComponentName = (type: string): string | null => {
  if (!isFieldTypeSupported(type)) {
    console.warn(`Unsupported field type: ${type}`)
    return null
  }
  return getFieldComponentName(type)
}

/**
 * Get the heading index for a group-heading field (for color rotation)
 * Counts how many group-heading fields appear before the given index
 */
const getHeadingIndex = (currentIndex: number): number => {
  let count = 0
  for (let i = 0; i < currentIndex; i++) {
    if (visibleFields.value[i]?.type === 'group-heading') {
      count++
    }
  }
  return count
}

/**
 * Update a field value
 */
const updateFieldValue = (fieldName: string, value: unknown) => {
  formValues.value[fieldName] = value
  // Clear any error for this field
  delete fieldErrors.value[fieldName]
  // Emit transformed values so parent sees external format, include meta
  emit('update:modelValue', transformOutputValues({ ...formValues.value }), { ...metaValues.value })
}

/**
 * Handle meta event from a field
 */
const handleFieldMeta = (fieldName: string, value: unknown) => {
  metaValues.value[fieldName] = value
  emit('meta', fieldName, value)
}

/**
 * Validate a single field
 */
const validateField = async (field: FieldDefinition): Promise<FieldValidationResult> => {
  if (!field.name) return { valid: true }

  const fieldRef = fieldRefs.value[field.name]

  if (fieldRef && typeof fieldRef.validate === 'function') {
    try {
      const result = await fieldRef.validate()
      if (!result.valid) {
        fieldErrors.value[field.name] = result.error || 'Invalid value'
      } else {
        delete fieldErrors.value[field.name]
      }
      return result
    } catch (error) {
      console.error(`Validation error for field ${field.name}:`, error)
      return { valid: false, error: 'Validation failed' }
    }
  }

  // Check required fields without custom validation
  if (!field.optional && !hasValidValue(field)) {
    const error = 'This field is required'
    fieldErrors.value[field.name] = error
    return { valid: false, error }
  }

  return { valid: true }
}

/**
 * Validate all visible fields
 */
const validateAllFields = async (): Promise<boolean> => {
  let allValid = true

  for (const field of visibleFields.value) {
    if (field.type === 'slot') continue

    const result = await validateField(field)
    if (!result.valid) {
      allValid = false
    }
  }

  return allValid
}

/**
 * Handle form submission
 */
const handleSubmit = async () => {
  if (isSubmitting.value || props.disabled) return

  isSubmitting.value = true

  try {
    // Run onSubmit for all fields with that method
    for (const field of visibleFields.value) {
      if (field.type === 'slot' || !field.name) continue

      const fieldRef = fieldRefs.value[field.name]
      if (fieldRef && typeof fieldRef.onSubmit === 'function') {
        try {
          const result = await fieldRef.onSubmit()
          if (result === false) {
            console.warn(`Field ${field.name} onSubmit returned false, halting form submission`)
            isSubmitting.value = false
            return
          }
        } catch (error) {
          console.error(`Field ${field.name} onSubmit failed:`, error)
          isSubmitting.value = false
          return
        }
      }
    }

    // Validate all fields
    const isValid = await validateAllFields()
    if (!isValid) {
      isSubmitting.value = false
      return
    }

    // Collect values from visible fields only
    const submittedValues: FormValues = {}
    for (const field of visibleFields.value) {
      if (field.type === 'slot' || !field.name) continue

      if (field.name in formValues.value) {
        submittedValues[field.name] = formValues.value[field.name]
      }
    }

    // Transform output values (split array fields back to individual values)
    const transformedOutput = transformOutputValues(submittedValues)

    emit('submit', transformedOutput, { ...metaValues.value })
  } finally {
    isSubmitting.value = false
  }
}

/**
 * Store ref to field component
 */
const setFieldRef = (fieldName: string, el: FieldComponent | null) => {
  if (el) {
    fieldRefs.value[fieldName] = el
  } else {
    delete fieldRefs.value[fieldName]
  }
}

// Initialize default values
setDefaultValues()

// Watch for changes to fields and reinitialize defaults
watch(
  () => props.fields,
  () => setDefaultValues(),
  { deep: true },
)

// Watch for external modelValue changes
watch(
  () => props.modelValue,
  (newValue) => {
    if (newValue) {
      const transformed = transformInputValues(newValue)
      formValues.value = { ...formValues.value, ...transformed }
    }
  },
  { deep: true },
)

// Expose submit method
defineExpose({
  submit: handleSubmit,
  validate: validateAllFields,
  values: formValues,
  meta: metaValues,
})
</script>

<template>
  <form class="dynamic-form" @submit.prevent="handleSubmit">
    <div
      v-for="(field, index) in visibleFields"
      :key="`form-field-${field.name}-${index}`"
      class="dynamic-form-field"
    >
      <!-- Slot type: render named slot -->
      <div v-if="field.type === 'slot'" style="padding-bottom: 1rem;">
        <slot :name="field.name" />
      </div>

      <!-- Group heading: render heading with rotating colors -->
      <template v-else-if="field.type === 'group-heading'">
        <component
          :is="getComponentName(field.type)"
          :label="field.label"
          :heading-index="getHeadingIndex(index)"
        />
      </template>

      <!-- Hidden fields: render without wrapper -->
      <template v-else-if="field.type === 'hidden' && field.name">
        <component
          :is="getComponentName(field.type)"
          :ref="(el: FieldComponent) => setFieldRef(field.name!, el)"
          :model-value="formValues[field.name]"
          @update:model-value="(value: unknown) => updateFieldValue(field.name!, value)"
          @meta="(value: unknown) => handleFieldMeta(field.name!, value)"
          v-bind="field.props"
        />
      </template>

      <!-- Field types: render field component with wrapper -->
      <template v-else-if="field.name">
        <Field
          :label="field.label"
          :help="field.help"
          :required="!field.optional"
          :error="fieldErrors[field.name]"
          :id="field.name"
        >
          <component
            :is="getComponentName(field.type)"
            :ref="(el: FieldComponent) => setFieldRef(field.name!, el)"
            :model-value="formValues[field.name]"
            @update:model-value="(value: unknown) => updateFieldValue(field.name!, value)"
            @meta="(value: unknown) => handleFieldMeta(field.name!, value)"
            v-bind="field.props"
            :disabled="disabled || field.props?.disabled"
            :required="!field.optional"
          />
        </Field>
      </template>
    </div>

    <div v-if="showButton" class="dynamic-form-actions">
      <button
        type="submit"
        class="dynamic-form-submit"
        :disabled="disabled || isSubmitting"
      >
        {{ isSubmitting ? 'Submitting...' : buttonText }}
      </button>
    </div>
  </form>
</template>

<style scoped>
.dynamic-form {
  width: 100%;
}

.dynamic-form-field {
  width: 100%;
}

.dynamic-form-actions {
  margin-top: 24px;
  display: flex;
  justify-content: center;
}

.dynamic-form-submit {
  padding: 14px 32px;
  border: none;
  border-radius: 12px;
  font-size: 1rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s ease;
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
  color: var(--brand-lamp-white);
  box-shadow: 0 4px 12px rgba(68, 108, 156, 0.3);
  min-width: 160px;
}

.dynamic-form-submit:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 6px 20px rgba(68, 108, 156, 0.4);
}

.dynamic-form-submit:active:not(:disabled) {
  transform: translateY(0);
}

.dynamic-form-submit:disabled {
  opacity: 0.6;
  cursor: not-allowed;
  transform: none;
}

/* Mobile optimizations */
@media (max-width: 480px) {
  .dynamic-form-submit {
    width: 100%;
    padding: 16px 24px;
  }
}
</style>
