<script setup lang="ts">
import IconButton from './IconButton.vue'
import type { FieldValidationResult } from '@/types'

interface Props {
  modelValue: number
  min?: number
  max?: number
  placeholder?: string
  disabled?: boolean
  required?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  min: 0,
  max: 100,
  placeholder: '',
  disabled: false,
  required: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: number]
}>()

const updateValue = (event: Event) => {
  if (props.disabled) return
  const target = event.target as HTMLInputElement
  let value = parseInt(target.value) || 0

  // Clamp to min/max
  value = Math.max(props.min, Math.min(props.max, value))

  emit('update:modelValue', value)
}

const increment = () => {
  if (props.disabled) return
  emit('update:modelValue', Math.min(props.modelValue + 1, props.max))
}

const decrement = () => {
  if (props.disabled) return
  emit('update:modelValue', Math.max(props.modelValue - 1, props.min))
}

// Validation method exposed to form
const validate = (): FieldValidationResult => {
  const value = props.modelValue

  if (props.required && (value === undefined || value === null)) {
    return { valid: false, error: 'This field is required' }
  }

  if (value < props.min) {
    return { valid: false, error: `Minimum value is ${props.min}` }
  }

  if (value > props.max) {
    return { valid: false, error: `Maximum value is ${props.max}` }
  }

  return { valid: true }
}

defineExpose({ validate })
</script>

<template>
  <div class="number-input-container">
    <input
      type="number"
      :value="modelValue"
      @input="updateValue"
      :min="props.min"
      :max="props.max"
      :placeholder="props.placeholder"
      :disabled="disabled"
      class="number-input"
      :class="{ disabled: disabled }"
    />
    <div class="number-input-buttons">
      <IconButton
        icon="minus"
        variant="minus"
        title="Decrease value"
        :disabled="disabled || modelValue <= props.min"
        @click="decrement"
      />
      <IconButton
        icon="plus"
        variant="plus"
        title="Increase value"
        :disabled="disabled || modelValue >= props.max"
        @click="increment"
      />
    </div>
  </div>
</template>

<style scoped>
.number-input-container {
  display: flex;
  align-items: center;
  gap: 12px;
  height: 52px;
  width: 100%;
}

.number-input {
  flex: 1;
  padding: 14px 16px;
  border: 2px solid var(--color-background-mute);
  border-radius: 12px;
  font-size: 1rem;
  font-weight: 500;
  transition: all 0.2s ease;
  background-color: var(--color-background);
  color: var(--color-text);
  -webkit-appearance: none;
  -moz-appearance: textfield;
  appearance: none;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
  height: 52px;
}

.number-input:hover:not(:disabled) {
  border-color: var(--color-border-hover);
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
  background-color: var(--color-background-soft);
}

.number-input:focus {
  outline: none;
  border-color: var(--brand-aurora-blue);
  box-shadow: 0 0 0 3px rgba(68, 108, 156, 0.2);
  background-color: var(--color-background);
}

.number-input::placeholder {
  color: var(--color-text);
  opacity: 0.6;
  font-weight: 500;
}

/* Hide default number input arrows */
.number-input::-webkit-outer-spin-button,
.number-input::-webkit-inner-spin-button {
  -webkit-appearance: none;
  appearance: none;
  margin: 0;
}

.number-input-buttons {
  display: flex;
  flex-direction: row;
  gap: 8px;
  flex-shrink: 0;
  align-items: center;
}

.disabled {
  opacity: 0.4;
  cursor: not-allowed;
  pointer-events: none;
}

/* Disabled state for number input */
.number-input.disabled,
.number-input:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  background-color: var(--color-background-mute);
}

.number-input.disabled:hover,
.number-input:disabled:hover {
  border-color: var(--color-background-mute);
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
  background-color: var(--color-background-mute);
}

.number-input.disabled:focus,
.number-input:disabled:focus {
  border-color: var(--color-background-mute);
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
  background-color: var(--color-background-mute);
}

/* Mobile optimizations */
@media (max-width: 768px) {
  .number-input {
    padding: 16px 18px;
    font-size: 1.1rem;
  }

  .number-input-container {
    gap: 14px;
  }
}

@media (max-width: 480px) {
  .number-input {
    padding: 18px 20px;
    font-size: 1.2rem;
  }

  .number-input-container {
    gap: 16px;
  }

  .number-input-buttons {
    gap: 10px;
  }
}
</style>
