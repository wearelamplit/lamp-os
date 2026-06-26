<script setup lang="ts">
// ponytail: web config renders only the common expression fields (target,
// colors, interval). Per-type params (durationMin, pulseSpeed, …) stay
// BLE-only; ExpressionsList round-trips them untouched. Add a control here
// only when the web tool needs to own that knob.
import FormField from '../FormField.vue'
import ColorPicker from '../ColorPicker.vue'
import NumberSlider from '../NumberSlider.vue'
import type { Expression } from '../../types'

const props = withDefaults(
  defineProps<{
    expression: Expression
    maxColors?: number
    disabled?: boolean
  }>(),
  { maxColors: 5, disabled: false },
)

// `preview` carries the live palette so the parent can light the lamp with
// these colors while editing (reuses the flat baseColors WS path).
const emit = defineEmits<{
  update: [updates: Partial<Expression>]
  preview: [colors: string[]]
}>()

const targetOptions = [
  { value: 1, label: 'Shade' },
  { value: 2, label: 'Base' },
  { value: 3, label: 'Both' },
]

const updateColor = (index: number, value: string) => {
  const colors = [...props.expression.colors]
  colors[index] = value
  emit('update', { colors })
  emit('preview', colors)
}

const addColor = () => {
  const colors = [...props.expression.colors, '#FF0000FF']
  emit('update', { colors })
  emit('preview', colors)
}

const removeColor = (index: number) => {
  const colors = props.expression.colors.filter((_, i) => i !== index)
  emit('update', { colors })
  emit('preview', colors)
}

const onIntervalMin = (value: number) => {
  const intervalMax = Math.max(value, props.expression.intervalMax)
  emit('update', { intervalMin: value, intervalMax })
}

const onIntervalMax = (value: number) => {
  const intervalMin = Math.min(value, props.expression.intervalMin)
  emit('update', { intervalMin, intervalMax: value })
}
</script>

<template>
  <div class="config">
    <FormField label="Target" id="expr-target">
      <div class="targets">
        <button
          v-for="opt in targetOptions"
          :key="opt.value"
          type="button"
          class="target"
          :class="{ active: expression.target === opt.value }"
          :disabled="disabled"
          @click="emit('update', { target: opt.value })"
        >
          {{ opt.label }}
        </button>
      </div>
    </FormField>

    <FormField label="Colors (randomly selected)" id="expr-colors">
      <div class="colors">
        <div v-for="(color, index) in expression.colors" :key="index" class="color-row">
          <ColorPicker
            :model-value="color"
            :disabled="disabled"
            @update:model-value="(value) => updateColor(index, value)"
          />
          <button
            v-if="expression.colors.length > 1"
            type="button"
            class="remove"
            :disabled="disabled"
            aria-label="Remove color"
            @click="removeColor(index)"
          >
            ×
          </button>
        </div>
        <button
          v-if="expression.colors.length < maxColors"
          type="button"
          class="add"
          :disabled="disabled"
          @click="addColor"
        >
          + Add color
        </button>
      </div>
    </FormField>

    <FormField label="Trigger interval (min)" id="expr-interval-min">
      <NumberSlider
        id="expr-interval-min"
        :model-value="expression.intervalMin"
        :min="30"
        :max="3600"
        append="s"
        :disabled="disabled"
        @update:model-value="onIntervalMin"
      />
    </FormField>

    <FormField label="Trigger interval (max)" id="expr-interval-max">
      <NumberSlider
        id="expr-interval-max"
        :model-value="expression.intervalMax"
        :min="30"
        :max="3600"
        append="s"
        :disabled="disabled"
        @update:model-value="onIntervalMax"
      />
    </FormField>
  </div>
</template>

<style scoped>
.config {
  display: flex;
  flex-direction: column;
  gap: 16px;
  padding-top: 12px;
}

.targets {
  display: flex;
  gap: 8px;
}

.target {
  flex: 1;
  padding: 8px 12px;
  background: rgba(255, 255, 255, 0.05);
  color: var(--brand-light-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 4px;
  cursor: pointer;
}

.target.active {
  background: rgba(64, 176, 0, 0.2);
  color: var(--brand-lumen-green);
  border-color: var(--brand-lumen-green);
}

.target:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.colors {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.color-row {
  display: flex;
  align-items: center;
  gap: 12px;
}

.remove {
  width: 32px;
  height: 32px;
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
  border: 1px solid #ef4444;
  border-radius: 4px;
  font-size: 1.5rem;
  line-height: 1;
  cursor: pointer;
}

.add {
  align-self: flex-start;
  padding: 8px 16px;
  background: rgba(64, 176, 0, 0.1);
  color: var(--brand-lumen-green);
  border: 1px dashed var(--brand-lumen-green);
  border-radius: 4px;
  cursor: pointer;
}
</style>
