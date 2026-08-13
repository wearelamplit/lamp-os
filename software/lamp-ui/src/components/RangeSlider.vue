<template>
  <div class="range-slider-group">
    <span class="range-slider-value">{{
      format
        ? `${format(minValue)} – ${format(maxValue)}`
        : `${prepend}${minValue}${append} – ${prepend}${maxValue}${append}`
    }}</span>
    <div class="range-slider-track">
      <div class="range-slider-rail"></div>
      <div class="range-slider-fill" :style="fillStyle"></div>
      <input
        :id="id && `${id}-min`"
        type="range"
        class="range-slider-input"
        :min="min"
        :max="max"
        :step="step"
        :value="minValue"
        :disabled="disabled"
        :style="minThumbStyle"
        @input="onMinInput"
      />
      <input
        :id="id && `${id}-max`"
        type="range"
        class="range-slider-input"
        :min="min"
        :max="max"
        :step="step"
        :value="maxValue"
        :disabled="disabled"
        :style="maxThumbStyle"
        @input="onMaxInput"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

interface Props {
  minValue: number
  maxValue: number
  id?: string
  min?: number
  max?: number
  step?: number
  color?: string
  append?: string
  prepend?: string
  disabled?: boolean
  /** Overrides the `prepend + value + append` label when set. */
  format?: (v: number) => string
}

const props = withDefaults(defineProps<Props>(), {
  min: 0,
  max: 255,
  step: 1,
  color: '#666666',
  append: '',
  prepend: '',
  disabled: false,
})

const emit = defineEmits<{
  'update:minValue': [value: number]
  'update:maxValue': [value: number]
}>()

const onMinInput = (e: Event) =>
  emit('update:minValue', Number((e.target as HTMLInputElement).value))
const onMaxInput = (e: Event) =>
  emit('update:maxValue', Number((e.target as HTMLInputElement).value))

const pct = (v: number) => {
  const span = props.max - props.min || 1
  return ((v - props.min) / span) * 100
}

const fillStyle = computed(() => ({
  left: `${pct(props.minValue)}%`,
  width: `${Math.max(0, pct(props.maxValue) - pct(props.minValue))}%`,
}))

const thumbStyle = computed(() => ({
  '--slider-thumb-color': props.color,
}))

// When the thumbs coincide, the max thumb's default DOM-order stacking
// buries the min thumb underneath. Once min is at or past the range
// midpoint, give it stacking priority instead so it stays grabbable.
const minOnTop = computed(() => pct(props.minValue) >= 50)
const minThumbStyle = computed(() => ({ ...thumbStyle.value, zIndex: minOnTop.value ? 2 : 1 }))
const maxThumbStyle = computed(() => ({ ...thumbStyle.value, zIndex: minOnTop.value ? 1 : 2 }))
</script>

<style scoped>
.range-slider-group {
  display: flex;
  flex-direction: column;
  gap: 10px;
  width: 100%;
}

.range-slider-value {
  font-weight: 600;
  color: var(--brand-lamp-white);
  font-size: 14px;
  text-align: center;
}

.range-slider-track {
  position: relative;
  height: 36px;
  display: flex;
  align-items: center;
}

.range-slider-rail,
.range-slider-fill {
  position: absolute;
  top: 50%;
  height: 8px;
  border-radius: 4px;
  transform: translateY(-50%);
  pointer-events: none;
}

.range-slider-rail {
  left: 0;
  right: 0;
  background: var(--brand-ash-grey);
}

.range-slider-fill {
  background: var(--brand-aurora-blue);
}

/* Two overlaid range inputs share one track: the input itself ignores
   pointer events so the invisible full-width track never grabs a drag,
   only its thumb (re-enabled below) does. */
.range-slider-input {
  position: absolute;
  left: 0;
  width: 100%;
  height: 36px;
  margin: 0;
  background: transparent;
  appearance: none;
  -webkit-appearance: none;
  pointer-events: none;
  cursor: pointer;
}

.range-slider-input::-webkit-slider-runnable-track {
  background: transparent;
}

.range-slider-input::-moz-range-track {
  background: transparent;
}

.range-slider-input::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  background: var(--slider-thumb-color, #666666);
  pointer-events: auto;
}

.range-slider-input::-moz-range-thumb {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  cursor: pointer;
  border: none;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
  background: var(--slider-thumb-color, #666666);
  pointer-events: auto;
}

.range-slider-input:disabled {
  pointer-events: none;
}

.range-slider-input:disabled::-webkit-slider-thumb,
.range-slider-input:disabled::-moz-range-thumb {
  opacity: 0.5;
  cursor: not-allowed;
}

/* Mobile optimizations */
@media (max-width: 480px) {
  .range-slider-value {
    font-size: 13px;
  }

  .range-slider-input::-webkit-slider-thumb,
  .range-slider-input::-moz-range-thumb {
    width: 32px;
    height: 32px;
  }
}
</style>
