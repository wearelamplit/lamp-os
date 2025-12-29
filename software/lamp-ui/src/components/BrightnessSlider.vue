<template>
  <NumberSlider
    v-model="localValue"
    :id="id"
    :min="min"
    :max="max"
    :append="append"
    :prepend="prepend"
    :color="brightnessIndicator"
    :disabled="disabled"
  />
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import NumberSlider from '@/components/fields/NumberSlider.vue'

interface Props {
  modelValue: number
  id: string
  min?: number
  max?: number
  append?: string
  prepend?: string
  disabled?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  min: 0,
  max: 100,
  append: '',
  prepend: '',
  disabled: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: number]
}>()

const localValue = ref(props.modelValue)

// Brightness indicator logic - creates a color that transitions from black to #e1a44a to white
const brightnessIndicator = computed(() => {
  const brightness = localValue.value / props.max
  if (brightness < 0.5) {
    // Scale between black (#000000) and #e1a44a
    // For brightness 0 -> 0.5, interpolate between black and #e1a44a
    const t = brightness / 0.5
    const r = Math.round(0 + (225 - 0) * t) // 0 to 225 (e1 in hex)
    const g = Math.round(0 + (164 - 0) * t) // 0 to 164 (a4 in hex)
    const b = Math.round(0 + (74 - 0) * t) // 0 to 74 (4a in hex)
    return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`
  } else {
    // Scale between #e1a44a and white (#ffffff)
    // For brightness 0.5 -> 1, interpolate between #e1a44a and white
    const t = (brightness - 0.5) / 0.5
    const r = Math.round(225 + (255 - 225) * t) // 225 to 255
    const g = Math.round(164 + (255 - 164) * t) // 164 to 255
    const b = Math.round(74 + (255 - 74) * t) // 74 to 255
    return `#${r.toString(16).padStart(2, '0')}${g.toString(16).padStart(2, '0')}${b.toString(16).padStart(2, '0')}`
  }
})

const updateValue = (value: number) => {
  emit('update:modelValue', value)
}

watch(
  () => props.modelValue,
  (newValue) => {
    localValue.value = newValue
  },
)

watch(localValue, (newValue) => {
  updateValue(newValue)
})
</script>
