<script setup lang="ts">
import { ref } from 'vue'
import type { FieldValidationResult } from '@/types'

interface Props {
  modelValue: string
  placeholder?: string
  maxLength?: number
  minLength?: number
  disabled?: boolean
  required?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  placeholder: '',
  maxLength: undefined,
  minLength: undefined,
  disabled: false,
  required: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: string]
}>()

const showPassword = ref(false)

const toggleVisibility = () => {
  showPassword.value = !showPassword.value
}

const updateValue = (event: Event) => {
  const target = event.target as HTMLInputElement
  let value = target.value

  // Apply max length if specified
  if (props.maxLength && value.length > props.maxLength) {
    value = value.substring(0, props.maxLength)
  }

  // Update the input field's value to reflect the validated result
  target.value = value

  emit('update:modelValue', value)
}

// Validation method exposed to form
const validate = (): FieldValidationResult => {
  const value = props.modelValue || ''

  if (props.required && !value.trim()) {
    return { valid: false, error: 'This field is required' }
  }

  if (props.minLength && value.length < props.minLength) {
    return { valid: false, error: `Minimum ${props.minLength} characters required` }
  }

  if (props.maxLength && value.length > props.maxLength) {
    return { valid: false, error: `Maximum ${props.maxLength} characters allowed` }
  }

  return { valid: true }
}

defineExpose({ validate })
</script>

<template>
  <div class="password-input-wrapper">
    <input
      :type="showPassword ? 'text' : 'password'"
      :value="modelValue"
      @input="updateValue"
      :placeholder="props.placeholder"
      :maxlength="props.maxLength"
      :minlength="props.minLength"
      :disabled="disabled"
      class="password-input"
      :class="{ disabled: disabled }"
    />
    <button
      type="button"
      class="visibility-toggle"
      @click="toggleVisibility"
      :disabled="disabled"
      :title="showPassword ? 'Hide password' : 'Show password'"
      aria-label="Toggle password visibility"
    >
      <!-- Eye open icon (password hidden - click to show) -->
      <svg v-if="!showPassword" width="20" height="20" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M12 5C7 5 2.73 8.11 1 12.5C2.73 16.89 7 20 12 20C17 20 21.27 16.89 23 12.5C21.27 8.11 17 5 12 5Z" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
        <circle cx="12" cy="12.5" r="3.5" stroke="currentColor" stroke-width="1.5"/>
      </svg>
      <!-- Eye closed icon (password visible - click to hide) -->
      <svg v-else width="20" height="20" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M4 4L20 20" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
        <path d="M12 5C7 5 2.73 8.11 1 12.5C1.82 14.54 3.21 16.27 5 17.5M12 20C17 20 21.27 16.89 23 12.5C22.18 10.46 20.79 8.73 19 7.5" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
        <path d="M9.88 9.88C9.33 10.43 9 11.18 9 12C9 13.66 10.34 15 12 15C12.82 15 13.57 14.67 14.12 14.12" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>
      </svg>
    </button>
  </div>
</template>

<style scoped>
.password-input-wrapper {
  position: relative;
  width: 100%;
}

.password-input {
  padding: 14px 48px 14px 16px;
  border: 2px solid var(--color-background-mute);
  border-radius: 12px;
  font-size: 1rem;
  font-weight: 500;
  transition: all 0.2s ease;
  background-color: var(--color-background);
  color: var(--color-text);
  width: 100%;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
  height: 52px;
}

.password-input:hover:not(:disabled) {
  border-color: var(--color-border-hover);
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
  background-color: var(--color-background-soft);
}

.password-input:focus {
  outline: none;
  border-color: var(--brand-aurora-blue);
  box-shadow: 0 0 0 3px rgba(68, 108, 156, 0.2);
  background-color: var(--color-background);
}

.password-input::placeholder {
  color: var(--color-text);
  opacity: 0.6;
  font-weight: 500;
}

.password-input.disabled,
.password-input:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  background-color: var(--color-background-mute);
}

.visibility-toggle {
  position: absolute;
  right: 12px;
  top: 50%;
  transform: translateY(-50%);
  background: none;
  border: none;
  padding: 6px;
  cursor: pointer;
  color: var(--brand-slate-grey);
  transition: color 0.2s ease;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 6px;
}

.visibility-toggle:hover:not(:disabled) {
  color: var(--brand-aurora-blue);
}

.visibility-toggle:focus {
  outline: none;
  color: var(--brand-aurora-blue);
}

.visibility-toggle:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

/* Mobile optimizations */
@media (max-width: 768px) {
  .password-input {
    padding: 16px 52px 16px 18px;
    font-size: 1.1rem;
  }

  .visibility-toggle {
    right: 14px;
  }

  .visibility-toggle svg {
    width: 22px;
    height: 22px;
  }
}

@media (max-width: 480px) {
  .password-input {
    padding: 18px 56px 18px 20px;
    font-size: 1.2rem;
  }

  .visibility-toggle {
    right: 16px;
  }

  .visibility-toggle svg {
    width: 24px;
    height: 24px;
  }
}
</style>

