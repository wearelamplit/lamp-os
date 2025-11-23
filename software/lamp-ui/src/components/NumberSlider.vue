<template>
  <div class="number-slider-group">
    <span class="number-slider-value">{{
      prepend === 'time' ? 'Time' : prepend + localValue + append
    }}</span>
    <input
      :id="id"
      v-model.number="localValue"
      type="range"
      :min="min"
      :max="max"
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

interface Props {
  modelValue: number
  id: string
  min?: number
  max?: number
  color?: string
  append?: string
  prepend?: string
  disabled?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  min: 0,
  max: 255,
  color: '#666666',
  append: '',
  prepend: '',
  disabled: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: number]
}>()

const localValue = ref(props.modelValue)

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
  // Only prevent page swiping, don't prevent slider functionality
  event.stopPropagation()
}

const handleTouchMove = (event: TouchEvent) => {
  // Allow the slider to work but prevent page swiping
  event.stopPropagation()
}

const handleTouchEnd = (event: TouchEvent) => {
  // Only prevent page swiping, don't prevent slider functionality
  event.stopPropagation()
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
  min-width: 40px;
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
  background: var(--slider-thumb-color, #666666);
}

.number-slider::-webkit-slider-thumb:hover {
  transform: scale(1.1);
  box-shadow: 0 6px 16px rgba(0, 0, 0, 0.5);
  background: var(--slider-thumb-hover-color, #777777);
}

.number-slider::-moz-range-thumb {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  border: 3px solid var(--brand-ash-grey);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  background: var(--slider-thumb-color, #666666);
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
    min-width: 35px;
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
    min-width: 30px;
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
