<template>
  <div class="number-slider-group">
    <span class="number-slider-value">{{ displayValue }}</span>
    <input
      :id="id"
      v-model.number="localValue"
      type="range"
      :min="min"
      :max="max"
      :step="stepValue"
      :disabled="disabled"
      class="number-slider"
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
  color?: string
  append?: string
  prepend?: string
  disabled?: boolean
  required?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  id: 'slider',
  min: 0,
  max: 255,
  step: 1,
  steps: undefined,
  color: 'var(--brand-glow-pink)',
  append: '',
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

const sliderStyle = computed(() => {
  return {
    '--slider-thumb-color': props.color,
    '--slider-thumb-hover-color': props.color,
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
.number-slider-group {
  display: flex;
  flex-direction: row;
  gap: 10px;
  align-items: center;
  width: 100%;
}

.number-slider-value {
  font-weight: 600;
  color: var(--brand-lamp-white);
  font-size: 14px;
  min-width: 50px;
  text-align: center;
  flex-shrink: 0;
}

.number-slider {
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

.number-slider:hover {
  background: var(--brand-slate-grey);
}

.number-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  transition: all 0.2s ease;
  background: var(--slider-thumb-color, var(--brand-glow-pink));
}

.number-slider::-webkit-slider-thumb:hover {
  transform: scale(1.1);
  box-shadow: 0 6px 16px rgba(0, 0, 0, 0.5);
  background: var(--slider-thumb-hover-color, var(--brand-glow-pink));
}

.number-slider::-moz-range-thumb {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  border: 3px solid var(--brand-ash-grey);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  background: var(--slider-thumb-color, var(--brand-glow-pink));
}

/* Disabled state styles */
.number-slider.disabled,
.number-slider:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  pointer-events: none;
}

.number-slider.disabled::-webkit-slider-thumb,
.number-slider:disabled::-webkit-slider-thumb {
  cursor: not-allowed;
  opacity: 0.5;
}

.number-slider.disabled::-moz-range-thumb,
.number-slider:disabled::-moz-range-thumb {
  cursor: not-allowed;
  opacity: 0.5;
}

/* Mobile optimizations */
@media (max-width: 480px) {
  .number-slider-group {
    gap: 8px;
  }

  .number-slider-value {
    font-size: 13px;
    min-width: 45px;
  }

  .number-slider::-webkit-slider-thumb {
    width: 32px;
    height: 32px;
  }

  .number-slider::-moz-range-thumb {
    width: 32px;
    height: 32px;
  }
}

@media (max-width: 360px) {
  .number-slider-group {
    gap: 6px;
  }

  .number-slider-value {
    font-size: 12px;
    min-width: 40px;
  }

  .number-slider::-webkit-slider-thumb {
    width: 28px;
    height: 28px;
  }

  .number-slider::-moz-range-thumb {
    width: 28px;
    height: 28px;
  }
}
</style>
