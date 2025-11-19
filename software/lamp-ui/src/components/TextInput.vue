<script setup lang="ts">
interface Props {
  modelValue: string
  placeholder?: string
  type?: string
  maxLength?: number
  minLength?: number
  pattern?: string
  autocapitalize?: boolean
  transform?: 'lowercase' | 'uppercase' | 'none'
}

const props = withDefaults(defineProps<Props>(), {
  placeholder: '',
  type: 'text',
  maxLength: undefined,
  pattern: undefined,
  transform: 'none',
})

const emit = defineEmits<{
  'update:modelValue': [value: string]
}>()

const updateValue = (event: Event) => {
  const target = event.target as HTMLInputElement
  let value = target.value

  // Apply transformation first
  if (props.transform === 'lowercase') {
    value = value.toLowerCase()
  } else if (props.transform === 'uppercase') {
    value = value.toUpperCase()
  }

  // Apply pattern filtering after transformation
  if (props.pattern) {
    const regex = new RegExp(props.pattern)
    value = value
      .split('')
      .filter((char) => regex.test(char))
      .join('')
  }

  // Apply max length if specified
  if (props.maxLength && value.length > props.maxLength) {
    value = value.substring(0, props.maxLength)
  }

  // Update the input field's value to reflect the validated result
  target.value = value

  emit('update:modelValue', value)
}
</script>

<template>
  <input
    :type="props.type"
    :value="modelValue"
    @input="updateValue"
    :placeholder="props.placeholder"
    :maxlength="props.maxLength"
    :minlength="props.minLength"
    :pattern="props.pattern"
    class="text-input"
  />
</template>

<style scoped>
.text-input {
  padding: 14px 16px;
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

.text-input:hover {
  border-color: var(--color-border-hover);
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
  background-color: var(--color-background-soft);
}

.text-input:focus {
  outline: none;
  border-color: var(--brand-aurora-blue);
  box-shadow: 0 0 0 3px rgba(68, 108, 156, 0.2);
  background-color: var(--color-background);
}

.text-input::placeholder {
  color: var(--color-text);
  opacity: 0.6;
  font-weight: 500;
}

/* Mobile optimizations */
@media (max-width: 768px) {
  .text-input {
    padding: 16px 18px;
    font-size: 1.1rem;
  }
}

@media (max-width: 480px) {
  .text-input {
    padding: 18px 20px;
    font-size: 1.2rem;
  }
}
</style>
