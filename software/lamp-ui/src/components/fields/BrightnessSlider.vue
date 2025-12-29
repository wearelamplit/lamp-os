<template>
  <div class="brightness-slider-group">
    <span class="brightness-slider-value">{{ displayValue }}</span>
    <input
      :id="id"
      v-model.number="localValue"
      type="range"
      :min="min"
      :max="max"
      :step="stepValue"
      :disabled="disabled"
      class="brightness-slider"
      :class="{ disabled: disabled }"
      :style="sliderStyle"
      @input="updateValue"
      @touchstart="handleTouchStart"
      @touchmove="handleTouchMove"
      @touchend="handleTouchEnd"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, watch, computed } from 'vue'
import type { FieldValidationResult } from '@/types'

interface Props {
  modelValue: number
  id?: string
  min?: number
  max?: number
  step?: number
  steps?: number
  append?: string
  prepend?: string
  disabled?: boolean
  required?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  id: 'brightness-slider',
  min: 0,
  max: 100,
  step: 1,
  steps: undefined,
  append: '%',
  prepend: '',
  disabled: false,
  required: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: number]
}>()

const localValue = ref(props.modelValue)

// Calculate step value based on steps prop or step prop
const stepValue = computed(() => {
  if (props.steps !== undefined) {
    return (props.max - props.min) / props.steps
  }
  return props.step
})

const displayValue = computed(() => {
  return `${props.prepend}${localValue.value}${props.append}`
})

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

const sliderStyle = computed(() => {
  return {
    '--slider-thumb-color': brightnessIndicator.value,
    '--slider-thumb-hover-color': brightnessIndicator.value,
  }
})

const updateValue = () => {
  emit('update:modelValue', localValue.value)
}

// Touch event handlers to prevent page swiping
const handleTouchStart = (event: TouchEvent) => {
  event.stopPropagation()
}

const handleTouchMove = (event: TouchEvent) => {
  event.stopPropagation()
}

const handleTouchEnd = (event: TouchEvent) => {
  event.stopPropagation()
}

// Validation method exposed to form
const validate = (): FieldValidationResult => {
  if (props.required && (localValue.value === undefined || localValue.value === null)) {
    return { valid: false, error: 'This field is required' }
  }
  if (localValue.value < props.min) {
    return { valid: false, error: `Minimum value is ${props.min}` }
  }
  if (localValue.value > props.max) {
    return { valid: false, error: `Maximum value is ${props.max}` }
  }
  return { valid: true }
}

watch(
  () => props.modelValue,
  (newValue) => {
    localValue.value = newValue
  },
)

watch(localValue, (newValue) => {
  emit('update:modelValue', newValue)
})

defineExpose({ validate })
</script>

<style scoped>
.brightness-slider-group {
  display: flex;
  flex-direction: row;
  gap: 10px;
  align-items: center;
  width: 100%;
}

.brightness-slider-value {
  font-weight: 600;
  color: var(--brand-lamp-white);
  font-size: 14px;
  min-width: 50px;
  text-align: center;
  flex-shrink: 0;
}

.brightness-slider {
  width: 100%;
  height: 8px;
  border-radius: 4px;
  background: var(--brand-ash-grey);
  outline: none;
  -webkit-appearance: none;
  appearance: none;
  cursor: pointer;
  transition: all 0.2s ease;
  flex: 1;
}

.brightness-slider:hover {
  background: var(--brand-slate-grey);
}

.brightness-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  transition: all 0.2s ease;
  background: var(--slider-thumb-color, #e1a44a);
}

.brightness-slider::-webkit-slider-thumb:hover {
  transform: scale(1.1);
  box-shadow: 0 6px 16px rgba(0, 0, 0, 0.5);
  background: var(--slider-thumb-hover-color, #e1a44a);
}

.brightness-slider::-moz-range-thumb {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  border: 3px solid var(--brand-ash-grey);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  background: var(--slider-thumb-color, #e1a44a);
}

/* Disabled state styles */
.brightness-slider.disabled,
.brightness-slider:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  pointer-events: none;
}

.brightness-slider.disabled::-webkit-slider-thumb,
.brightness-slider:disabled::-webkit-slider-thumb {
  cursor: not-allowed;
  opacity: 0.5;
}

.brightness-slider.disabled::-moz-range-thumb,
.brightness-slider:disabled::-moz-range-thumb {
  cursor: not-allowed;
  opacity: 0.5;
}

/* Mobile optimizations */
@media (max-width: 480px) {
  .brightness-slider-group {
    gap: 8px;
  }

  .brightness-slider-value {
    font-size: 13px;
    min-width: 45px;
  }

  .brightness-slider::-webkit-slider-thumb {
    width: 32px;
    height: 32px;
  }

  .brightness-slider::-moz-range-thumb {
    width: 32px;
    height: 32px;
  }
}

@media (max-width: 360px) {
  .brightness-slider-group {
    gap: 6px;
  }

  .brightness-slider-value {
    font-size: 12px;
    min-width: 40px;
  }

  .brightness-slider::-webkit-slider-thumb {
    width: 28px;
    height: 28px;
  }

  .brightness-slider::-moz-range-thumb {
    width: 28px;
    height: 28px;
  }
}
</style>

