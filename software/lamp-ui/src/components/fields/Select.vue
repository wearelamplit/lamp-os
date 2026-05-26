<script setup lang="ts">
import type { FieldValidationResult } from '@/types'

interface SelectOption {
  value: number | string
  label: string
}

interface Props {
  modelValue: number | string
  options: SelectOption[]
  disabled?: boolean
  required?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  disabled: false,
  required: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: number | string]
}>()

function onChange(event: Event) {
  const target = event.target as HTMLSelectElement
  const selected = props.options.find((o) => String(o.value) === target.value)
  if (selected) {
    emit('update:modelValue', selected.value)
  }
}

const validate = (): FieldValidationResult => {
  return { valid: true }
}

defineExpose({ validate })
</script>

<template>
  <select
    class="select-input"
    :value="String(modelValue)"
    :disabled="disabled"
    :required="required"
    @change="onChange"
  >
    <option
      v-for="opt in options"
      :key="String(opt.value)"
      :value="String(opt.value)"
    >
      {{ opt.label }}
    </option>
  </select>
</template>

<style scoped>
.select-input {
  width: 100%;
  padding: 0.5rem;
  border-radius: 4px;
  border: 1px solid var(--color-background-mute);
  background: var(--color-background-soft);
  color: var(--color-text);
  font-family: inherit;
  font-size: 1rem;
}
.select-input:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
</style>
