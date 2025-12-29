<!-- eslint-disable vue/multi-word-component-names -->
<script setup lang="ts">
import type { FieldValidationResult } from '@/types'

interface Props {
  modelValue: unknown
}

const props = defineProps<Props>()
const emit = defineEmits<{
  'update:modelValue': [value: unknown]
}>()

// Emit the modelValue immediately
emit('update:modelValue', props.modelValue)

// Validation method - hidden fields are always valid
const validate = (): FieldValidationResult => {
  return { valid: true }
}

defineExpose({ validate })
</script>

<template>
  <input type="hidden" :value="String(modelValue ?? '')" />
</template>

<style scoped>
/* No styles needed for hidden field */
</style>
