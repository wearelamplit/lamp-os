<template>
  <div class="config-container">
    <!-- Target Selection -->
    <FormField label="Target" id="expression-target">
      <div class="target-selector">
        <button
          v-for="option in targetOptions"
          :key="option.value"
          class="target-button"
          :class="{ active: expression.target === option.value }"
          @click="updateTarget(option.value)"
          :disabled="disabled"
        >
          {{ option.label }}
        </button>
      </div>
    </FormField>

    <!-- Colors Configuration -->
    <FormField :label="expression.type === 'breathing' ? 'Breathing Color' : 'Colors (Randomly Selected)'" id="expression-colors">
      <div class="colors-container">
        <div v-for="(color, index) in expression.colors" :key="index" class="color-item">
          <ColorPicker
            :model-value="color"
            @update:model-value="(value) => updateColor(index, value)"
            @preview="(value) => emit('preview-color', value, expression.target)"
            @open="() => emit('color-picker-open')"
            @close="() => emit('color-picker-close')"
            :disabled="disabled"
          />
          <button
            v-if="expression.colors.length > minColors"
            class="remove-color-button"
            @click="removeColor(index)"
            :disabled="disabled"
            aria-label="Remove color"
          >
            ×
          </button>
        </div>

        <button
          v-if="expression.colors.length < maxColors"
          class="add-color-button"
          @click="addColor"
          :disabled="disabled"
        >
          + Add Color
        </button>
      </div>
    </FormField>

    <!-- Interval Configuration -->
    <div v-if="expression.type !== 'breathing'" class="interval-section">
      <div class="interval-header">
        <span class="interval-label">Random Trigger Interval</span>
      </div>
      <div class="interval-container">
        <div class="interval-input">
          <label>Min</label>
          <input
            type="range"
            v-model.number="localIntervalMin"
            @input="handleIntervalMinChange"
            min="300"
            max="2700"
            step="60"
            :disabled="disabled"
            class="interval-slider"
          />
          <span class="interval-value">{{ formatInterval(localIntervalMin) }}</span>
        </div>

        <div class="interval-input">
          <label>Max</label>
          <input
            type="range"
            v-model.number="localIntervalMax"
            @input="handleIntervalMaxChange"
            min="300"
            max="2700"
            step="60"
            :disabled="disabled"
            class="interval-slider"
          />
          <span class="interval-value">{{ formatInterval(localIntervalMax) }}</span>
        </div>
      </div>
    </div>

    <!-- Expression-specific Configuration -->
    <FormField
      v-if="expression.type === 'glitchy'"
      label="Glitch Duration Range"
      id="expression-duration"
    >
      <div class="interval-container">
        <div class="interval-input">
          <label for="glitch-duration-min">Min</label>
          <input
            id="glitch-duration-min"
            v-model.number="localDurationMin"
            type="range"
            :min="1"
            :max="60"
            :step="1"
            :disabled="disabled"
            class="interval-slider"
            @input="handleDurationMinChange"
          />
          <span class="interval-value">{{ formatDuration(localDurationMin) }}</span>
        </div>
        <div class="interval-input">
          <label for="glitch-duration-max">Max</label>
          <input
            id="glitch-duration-max"
            v-model.number="localDurationMax"
            type="range"
            :min="1"
            :max="60"
            :step="1"
            :disabled="disabled"
            class="interval-slider"
            @input="handleDurationMaxChange"
          />
          <span class="interval-value">{{ formatDuration(localDurationMax) }}</span>
        </div>
        <span class="interval-summary"
          >{{ formatDuration(localDurationMin) }} - {{ formatDuration(localDurationMax) }}</span
        >
      </div>
    </FormField>

    <!-- Shifty-specific Configuration -->
    <FormField
      v-if="expression.type === 'shifty'"
      label="Fade Duration"
      id="expression-fade-duration"
    >
      <div class="duration-container">
        <NumberSlider
          id="fade-duration"
          :model-value="expression.fadeDuration || 60"
          @update:model-value="(value) => updateField('fadeDuration', value)"
          :min="30"
          :max="900"
          :step="15"
          :disabled="disabled"
          prepend="time"
        />
        <span class="duration-value">{{ formatInterval(expression.fadeDuration || 60) }}</span>
      </div>
    </FormField>

    <FormField
      v-if="expression.type === 'shifty'"
      label="Shifted Color Hold Time"
      id="expression-shift-duration"
    >
      <div class="interval-container">
        <div class="interval-input">
          <label>Min</label>
          <input
            type="range"
            v-model.number="localShiftDurationMin"
            @input="handleShiftDurationMinChange"
            min="300"
            max="1800"
            step="30"
            :disabled="disabled"
            class="interval-slider"
          />
          <span class="interval-value">{{ formatInterval(localShiftDurationMin) }}</span>
        </div>

        <div class="interval-input">
          <label>Max</label>
          <input
            type="range"
            v-model.number="localShiftDurationMax"
            @input="handleShiftDurationMaxChange"
            min="300"
            max="1800"
            step="30"
            :disabled="disabled"
            class="interval-slider"
          />
          <span class="interval-value">{{ formatInterval(localShiftDurationMax) }}</span>
        </div>
      </div>
    </FormField>

    <!-- Pulse-specific Configuration -->
    <FormField v-if="expression.type === 'pulse'" label="Pulse Speed" id="expression-pulse-speed">
      <div class="duration-container">
        <NumberSlider
          id="pulse-speed"
          :model-value="expression.pulseSpeed || 3"
          @update:model-value="(value) => updateField('pulseSpeed', value)"
          :min="1"
          :max="10"
          :step="1"
          :disabled="disabled"
          prepend="time"
        />
        <span class="duration-value">{{ expression.pulseSpeed || 3 }}s</span>
      </div>
    </FormField>

    <!-- Breathing-specific Configuration -->
    <FormField
      v-if="expression.type === 'breathing'"
      label="Breath Speed"
      id="expression-breath-speed"
    >
      <div class="duration-container">
        <NumberSlider
          id="breath-speed"
          :model-value="expression.breathSpeed || 10"
          @update:model-value="(value) => updateField('breathSpeed', value)"
          :min="5"
          :max="30"
          :step="1"
          :disabled="disabled"
          prepend="time"
        />
        <span class="duration-value">{{ expression.breathSpeed || 10 }}s</span>
      </div>
    </FormField>
  </div>
</template>

<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import FormField from '@/components/FormField.vue'
import ColorPicker from '@/components/ColorPicker.vue'
import NumberSlider from '@/components/NumberSlider.vue'

interface Expression {
  type: string
  enabled: boolean
  colors: string[]
  intervalMin: number
  intervalMax: number
  target: number
  duration?: number
  durationMin?: number
  durationMax?: number
  fadeDuration?: number
  shiftDurationMin?: number
  shiftDurationMax?: number
  pulseSpeed?: number
  breathSpeed?: number
}

type Scalar = null | boolean | number | string

interface ConfigSchema {
  colors?: {
    min: number
    max: number
  }
}

const props = defineProps<{
  expression: Expression
  configSchema: ConfigSchema
  disabled?: boolean
}>()

const emit = defineEmits<{
  update: [updates: Partial<Expression>]
  'preview-color': [color: string, target: number]
  'color-picker-open': []
  'color-picker-close': []
}>()

const targetOptions = [
  { value: 1, label: 'Shade' },
  { value: 2, label: 'Base' },
  { value: 3, label: 'Both' },
]

const minColors = computed(() => props.configSchema?.colors?.min || 1)
const maxColors = computed(() => props.configSchema?.colors?.max || 5)

// Local refs for smooth slider dragging
const localIntervalMin = ref(props.expression.intervalMin)
const localIntervalMax = ref(props.expression.intervalMax)
const localDurationMin = ref(props.expression.durationMin || 1)
const localDurationMax = ref(props.expression.durationMax || 3)
const localShiftDurationMin = ref(props.expression.shiftDurationMin || 300)
const localShiftDurationMax = ref(props.expression.shiftDurationMax || 600)

// Watch for external changes to sync local values
watch(
  () => props.expression.intervalMin,
  (newVal) => {
    localIntervalMin.value = newVal
  },
)

watch(
  () => props.expression.intervalMax,
  (newVal) => {
    localIntervalMax.value = newVal
  },
)

watch(
  () => props.expression.durationMin,
  (newVal) => {
    if (newVal !== undefined) localDurationMin.value = newVal
  },
)

watch(
  () => props.expression.durationMax,
  (newVal) => {
    if (newVal !== undefined) localDurationMax.value = newVal
  },
)

watch(
  () => props.expression.shiftDurationMin,
  (newVal) => {
    if (newVal !== undefined) localShiftDurationMin.value = newVal
  },
)

watch(
  () => props.expression.shiftDurationMax,
  (newVal) => {
    if (newVal !== undefined) localShiftDurationMax.value = newVal
  },
)

const updateField = (field: keyof Expression, value: Scalar) => {
  // The HTML range inputs already enforce min/max constraints through their attributes
  // No additional validation needed here
  emit('update', { [field]: value })
}

const updateTarget = (value: number) => {
  emit('update', { target: value })
}

const handleIntervalMinChange = () => {
  // If min exceeds max, set max equal to min
  if (localIntervalMin.value > localIntervalMax.value) {
    localIntervalMax.value = localIntervalMin.value
    emit('update', {
      intervalMin: localIntervalMin.value,
      intervalMax: localIntervalMax.value,
    })
  } else {
    emit('update', { intervalMin: localIntervalMin.value })
  }
}

const handleIntervalMaxChange = () => {
  // If max is less than min, set min equal to max
  if (localIntervalMax.value < localIntervalMin.value) {
    localIntervalMin.value = localIntervalMax.value
    emit('update', {
      intervalMin: localIntervalMin.value,
      intervalMax: localIntervalMax.value,
    })
  } else {
    emit('update', { intervalMax: localIntervalMax.value })
  }
}

const handleDurationMinChange = () => {
  // If min exceeds max, set max equal to min
  if (localDurationMin.value > localDurationMax.value) {
    localDurationMax.value = localDurationMin.value
    emit('update', {
      durationMin: localDurationMin.value,
      durationMax: localDurationMax.value,
    })
  } else {
    emit('update', { durationMin: localDurationMin.value })
  }
}

const handleDurationMaxChange = () => {
  // If max is less than min, set min equal to max
  if (localDurationMax.value < localDurationMin.value) {
    localDurationMin.value = localDurationMax.value
    emit('update', {
      durationMin: localDurationMin.value,
      durationMax: localDurationMax.value,
    })
  } else {
    emit('update', { durationMax: localDurationMax.value })
  }
}

const handleShiftDurationMinChange = () => {
  // If min exceeds max, set max equal to min
  if (localShiftDurationMin.value > localShiftDurationMax.value) {
    localShiftDurationMax.value = localShiftDurationMin.value
    emit('update', {
      shiftDurationMin: localShiftDurationMin.value,
      shiftDurationMax: localShiftDurationMax.value,
    })
  } else {
    emit('update', { shiftDurationMin: localShiftDurationMin.value })
  }
}

const handleShiftDurationMaxChange = () => {
  // If max is less than min, set min equal to max
  if (localShiftDurationMax.value < localShiftDurationMin.value) {
    localShiftDurationMin.value = Math.max(300, localShiftDurationMax.value)
    emit('update', {
      shiftDurationMin: localShiftDurationMin.value,
      shiftDurationMax: localShiftDurationMax.value,
    })
  } else {
    emit('update', { shiftDurationMax: localShiftDurationMax.value })
  }
}

const updateColor = (index: number, value: string) => {
  const newColors = [...props.expression.colors]
  newColors[index] = value
  emit('update', { colors: newColors })
}

const addColor = () => {
  const newColors = [...props.expression.colors, '#FFFFFFFF']
  emit('update', { colors: newColors })
}

const removeColor = (index: number) => {
  const newColors = props.expression.colors.filter((_, i) => i !== index)
  emit('update', { colors: newColors })
}

const formatInterval = (seconds: number): string => {
  if (seconds < 60) {
    return `${seconds}s`
  } else if (seconds < 3600) {
    const minutes = Math.floor(seconds / 60)
    return `${minutes}m`
  } else {
    const hours = Math.floor(seconds / 3600)
    const minutes = Math.floor((seconds % 3600) / 60)
    return minutes > 0 ? `${hours}h ${minutes}m` : `${hours}h`
  }
}

const formatDuration = (frames: number): string => {
  // Convert frames to seconds (30fps)
  const seconds = frames / 30
  if (seconds < 1) {
    return `${Math.round(seconds * 1000)}ms`
  } else if (seconds < 60) {
    return `${seconds.toFixed(1)}s`
  } else {
    const minutes = Math.floor(seconds / 60)
    const remainingSeconds = Math.floor(seconds % 60)
    return remainingSeconds > 0 ? `${minutes}m ${remainingSeconds}s` : `${minutes}m`
  }
}
</script>

<style scoped>
.config-container {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding-top: 12px;
}

.target-selector {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.target-button {
  flex: 1;
  min-width: 70px;
  padding: 8px 16px;
  background: rgba(255, 255, 255, 0.05);
  color: var(--brand-light-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s;
}

.target-button:hover:not(:disabled) {
  background: rgba(255, 255, 255, 0.1);
}

.target-button.active {
  background: rgba(64, 176, 0, 0.2);
  color: var(--brand-green);
  border-color: var(--brand-green);
}

.target-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.interval-section {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.interval-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 8px;
}

.interval-label {
  font-size: 14px;
  font-weight: 500;
  color: var(--brand-white);
}

.colors-container {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.color-item {
  display: flex;
  align-items: center;
  gap: 12px;
}

.remove-color-button {
  width: 32px;
  height: 32px;
  padding: 0;
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
  border: 1px solid #ef4444;
  border-radius: 4px;
  font-size: 1.5rem;
  line-height: 1;
  cursor: pointer;
  transition: all 0.2s;
}

.remove-color-button:hover:not(:disabled) {
  background: rgba(239, 68, 68, 0.3);
}

.remove-color-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.add-color-button {
  padding: 8px 16px;
  background: rgba(64, 176, 0, 0.1);
  color: var(--brand-green);
  border: 1px dashed var(--brand-green);
  border-radius: 4px;
  cursor: pointer;
  transition: all 0.2s;
  align-self: flex-start;
}

.add-color-button:hover:not(:disabled) {
  background: rgba(64, 176, 0, 0.2);
}

.add-color-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.interval-container {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.interval-input {
  display: flex;
  align-items: center;
  gap: 12px;
}

.interval-input label {
  min-width: 40px;
  color: var(--brand-light-grey);
  font-size: 0.875rem;
}

.interval-input :deep(.slider-container) {
  flex: 1;
}

.interval-slider {
  flex: 1;
  -webkit-appearance: none;
  appearance: none;
  height: 8px;
  border-radius: 4px;
  background: var(--brand-ash-grey);
  outline: none;
  cursor: pointer;
}

.interval-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 24px;
  height: 24px;
  border-radius: 50%;
  background: var(--brand-lamp-white);
  cursor: pointer;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.3);
}

.interval-slider::-webkit-slider-thumb:hover {
  transform: scale(1.1);
}

.interval-slider::-moz-range-thumb {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  background: var(--brand-lamp-white);
  cursor: pointer;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.3);
  border: none;
}

.interval-slider:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.interval-value {
  min-width: 60px;
  text-align: right;
  color: var(--brand-fog-grey);
  font-size: 0.875rem;
}

.duration-container {
  display: flex;
  align-items: center;
  gap: 12px;
}

.duration-container :deep(.slider-container) {
  flex: 1;
}

.duration-value {
  min-width: 80px;
  text-align: right;
  color: var(--brand-fog-grey);
  font-size: 0.875rem;
}
</style>
