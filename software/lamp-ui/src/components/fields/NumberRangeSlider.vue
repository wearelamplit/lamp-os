<template>
  <div class="number-range-slider-group">
    <div class="number-range-slider-values">
      <span class="number-range-slider-value">{{ prepend }}{{ sortedValues[0] }}{{ append }}</span>
      <span class="number-range-slider-separator">—</span>
      <span class="number-range-slider-value">{{ prepend }}{{ sortedValues[1] }}{{ append }}</span>
    </div>
    <div class="number-range-slider-container">
      <div class="number-range-slider-track" ref="trackRef">
        <div class="number-range-slider-range" :style="rangeStyle"></div>
        <input
          type="range"
          :id="`${id}-a`"
          v-model.number="thumbA"
          :min="min"
          :max="max"
          :step="stepValue"
          :disabled="disabled"
          class="number-range-slider"
          :style="sliderStyleA"
          @input="handleInput"
          @touchstart="handleTouchStart"
          @touchmove="handleTouchMove"
          @touchend="handleTouchEnd"
        />
        <input
          type="range"
          :id="`${id}-b`"
          v-model.number="thumbB"
          :min="min"
          :max="max"
          :step="stepValue"
          :disabled="disabled"
          class="number-range-slider"
          :style="sliderStyleB"
          @input="handleInput"
          @touchstart="handleTouchStart"
          @touchmove="handleTouchMove"
          @touchend="handleTouchEnd"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, computed } from 'vue'
import type { FieldValidationResult } from '@/types'

interface Props {
  modelValue: [number, number]
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
  id: 'range-slider',
  min: 0,
  max: 100,
  step: 1,
  steps: undefined,
  color: 'var(--brand-aurora-blue)',
  append: '',
  prepend: '',
  disabled: false,
  required: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: [number, number]]
}>()

const trackRef = ref<HTMLElement | null>(null)

// Two independent thumb values - they can move freely
const thumbA = ref(props.modelValue[0])
const thumbB = ref(props.modelValue[1])

// Sorted values - always [min, max] regardless of which thumb is where
const sortedValues = computed<[number, number]>(() => {
  const a = thumbA.value
  const b = thumbB.value
  return a <= b ? [a, b] : [b, a]
})

// Calculate step value based on steps prop or step prop
const stepValue = computed(() => {
  if (props.steps !== undefined) {
    return (props.max - props.min) / props.steps
  }
  return props.step
})

// Determine which thumb is the lower vs higher value
const thumbAIsLower = computed(() => thumbA.value <= thumbB.value)

// Style for thumb A - blue if lower, pink if higher
const sliderStyleA = computed(() => ({
  '--slider-thumb-color': thumbAIsLower.value ? 'var(--brand-aurora-blue)' : 'var(--brand-glow-pink)',
  '--slider-thumb-hover-color': thumbAIsLower.value ? 'var(--brand-aurora-blue)' : 'var(--brand-glow-pink)',
}))

// Style for thumb B - pink if higher, blue if lower
const sliderStyleB = computed(() => ({
  '--slider-thumb-color': thumbAIsLower.value ? 'var(--brand-glow-pink)' : 'var(--brand-aurora-blue)',
  '--slider-thumb-hover-color': thumbAIsLower.value ? 'var(--brand-glow-pink)' : 'var(--brand-aurora-blue)',
}))

// Calculate the range highlight style based on sorted values - gradient from blue to pink
const rangeStyle = computed(() => {
  const range = props.max - props.min
  const minPercent = ((sortedValues.value[0] - props.min) / range) * 100
  const maxPercent = ((sortedValues.value[1] - props.min) / range) * 100
  return {
    left: `${minPercent}%`,
    width: `${maxPercent - minPercent}%`,
    background: 'linear-gradient(to right, var(--brand-aurora-blue), var(--brand-glow-pink))',
  }
})

const handleInput = () => {
  // Emit the sorted values so output is always [lower, higher]
  emit('update:modelValue', [...sortedValues.value] as [number, number])
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
  if (props.required && (thumbA.value === undefined || thumbB.value === undefined)) {
    return { valid: false, error: 'This field is required' }
  }
  return { valid: true }
}

// Watch for external modelValue changes
watch(
  () => props.modelValue,
  (newValue) => {
    if (Array.isArray(newValue) && newValue.length === 2) {
      // Assign to thumbs - order doesn't matter as we sort on output
      // Check if current thumbs match the sorted values, if not update them
      const currentSorted = sortedValues.value
      if (newValue[0] !== currentSorted[0] || newValue[1] !== currentSorted[1]) {
        thumbA.value = newValue[0]
        thumbB.value = newValue[1]
      }
    }
  },
)

defineExpose({ validate })
</script>

<style scoped>
.number-range-slider-group {
  display: flex;
  flex-direction: column;
  gap: 12px;
  width: 100%;
}

.number-range-slider-values {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
}

.number-range-slider-value {
  font-weight: 600;
  color: var(--brand-lamp-white);
  font-size: 14px;
  min-width: 50px;
  text-align: center;
}

.number-range-slider-separator {
  color: var(--brand-slate-grey);
  font-size: 14px;
}

.number-range-slider-container {
  position: relative;
  width: 100%;
  padding: 0 18px;
}

.number-range-slider-track {
  position: relative;
  width: 100%;
  height: 8px;
  background: var(--brand-ash-grey);
  border-radius: 4px;
}

.number-range-slider-range {
  position: absolute;
  height: 100%;
  border-radius: 4px;
  pointer-events: none;
  z-index: 1;
}

.number-range-slider {
  position: absolute;
  top: 50%;
  left: 0;
  width: 100%;
  height: 8px;
  transform: translateY(-50%);
  -webkit-appearance: none;
  appearance: none;
  background: transparent;
  pointer-events: none;
  z-index: 2;
}

.number-range-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  transition: all 0.2s ease;
  background: var(--slider-thumb-color, #666666);
  pointer-events: auto;
  position: relative;
  z-index: 3;
}

.number-range-slider::-webkit-slider-thumb:hover {
  transform: scale(1.1);
  box-shadow: 0 6px 16px rgba(0, 0, 0, 0.5);
  background: var(--slider-thumb-hover-color, #777777);
}

.number-range-slider::-moz-range-thumb {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  border: none;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  background: var(--slider-thumb-color, #666666);
  pointer-events: auto;
}

/* Disabled state styles */
.number-range-slider:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.number-range-slider:disabled::-webkit-slider-thumb {
  cursor: not-allowed;
  opacity: 0.5;
}

.number-range-slider:disabled::-moz-range-thumb {
  cursor: not-allowed;
  opacity: 0.5;
}

/* Mobile optimizations */
@media (max-width: 480px) {
  .number-range-slider-group {
    gap: 10px;
  }

  .number-range-slider-value {
    font-size: 13px;
    min-width: 45px;
  }

  .number-range-slider::-webkit-slider-thumb {
    width: 32px;
    height: 32px;
  }

  .number-range-slider::-moz-range-thumb {
    width: 32px;
    height: 32px;
  }
}

@media (max-width: 360px) {
  .number-range-slider-value {
    font-size: 12px;
    min-width: 40px;
  }

  .number-range-slider::-webkit-slider-thumb {
    width: 28px;
    height: 28px;
  }

  .number-range-slider::-moz-range-thumb {
    width: 28px;
    height: 28px;
  }
}
</style>
