<script setup lang="ts">
// Renders the subset of the catalog the web UI owns: colors, interval,
// duration, plain int sliders, and enum selects. Zones, requiresZoning
// params, and the invert/label cosmetics are skipped; ExpressionsList
// round-trips every un-rendered instance key untouched.
import { computed } from 'vue'
import FormField from '../FormField.vue'
import ColorPicker from '../ColorPicker.vue'
import NumberSlider from '../NumberSlider.vue'
import { resolveBound } from './catalog'
import type { Expression, ExpressionDescriptor, CatalogParam } from '../../types'

const props = withDefaults(
  defineProps<{
    expression: Expression
    descriptor: ExpressionDescriptor
    basePx: number
    shadePx: number
    disabled?: boolean
  }>(),
  { disabled: false },
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

const pixelCount = computed(() =>
  props.expression.target === 1 ? props.shadePx : props.basePx,
)

const maxColors = computed(() => props.descriptor.colors.max)
const minColors = computed(() => (props.descriptor.colors.inheritsSurface ? 0 : 1))

const shownParams = computed(() =>
  (props.descriptor.params ?? []).filter(
    (p) => !p.requiresZoning && (p.type === 'int' || p.type === 'enum'),
  ),
)

const paramMax = (p: CatalogParam) => resolveBound(p.max, pixelCount.value, p.min ?? 0)
const paramValue = (p: CatalogParam) => Number(props.expression[p.key] ?? p.min ?? 0)

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

const onDurationMin = (value: number) => {
  const d = props.descriptor.duration
  if (!d?.minKey || !d.maxKey) return
  const hi = Math.max(value, Number(props.expression[d.maxKey] ?? value))
  emit('update', { [d.minKey]: value, [d.maxKey]: hi })
}

const onDurationMax = (value: number) => {
  const d = props.descriptor.duration
  if (!d?.minKey || !d.maxKey) return
  const lo = Math.min(value, Number(props.expression[d.minKey] ?? value))
  emit('update', { [d.minKey]: lo, [d.maxKey]: value })
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

    <FormField
      v-if="descriptor.colors.max > 0"
      :label="descriptor.colors.label || 'Colors (randomly selected)'"
      id="expr-colors"
    >
      <div class="colors">
        <div v-for="(color, index) in expression.colors" :key="index" class="color-row">
          <ColorPicker
            :model-value="color"
            :disabled="disabled"
            @update:model-value="(value) => updateColor(index, value)"
          />
          <button
            v-if="expression.colors.length > minColors"
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

    <template v-if="descriptor.interval">
      <FormField :label="`${descriptor.interval.label || 'Trigger interval'} (min)`" id="expr-interval-min">
        <NumberSlider
          id="expr-interval-min"
          :model-value="expression.intervalMin"
          :min="descriptor.interval.min"
          :max="descriptor.interval.max"
          :step="descriptor.interval.step"
          :append="descriptor.interval.unit || ''"
          :disabled="disabled"
          @update:model-value="onIntervalMin"
        />
      </FormField>

      <FormField :label="`${descriptor.interval.label || 'Trigger interval'} (max)`" id="expr-interval-max">
        <NumberSlider
          id="expr-interval-max"
          :model-value="expression.intervalMax"
          :min="descriptor.interval.min"
          :max="descriptor.interval.max"
          :step="descriptor.interval.step"
          :append="descriptor.interval.unit || ''"
          :disabled="disabled"
          @update:model-value="onIntervalMax"
        />
      </FormField>
    </template>

    <template v-if="descriptor.duration?.minKey && descriptor.duration?.maxKey">
      <FormField :label="`${descriptor.duration.label || 'Duration'} (min)`" id="expr-duration-min">
        <NumberSlider
          id="expr-duration-min"
          :model-value="Number(expression[descriptor.duration.minKey] ?? descriptor.duration.default[0])"
          :min="descriptor.duration.min"
          :max="descriptor.duration.max"
          :step="descriptor.duration.step"
          :append="descriptor.duration.unit || ''"
          :disabled="disabled"
          @update:model-value="onDurationMin"
        />
      </FormField>

      <FormField :label="`${descriptor.duration.label || 'Duration'} (max)`" id="expr-duration-max">
        <NumberSlider
          id="expr-duration-max"
          :model-value="Number(expression[descriptor.duration.maxKey] ?? descriptor.duration.default[1])"
          :min="descriptor.duration.min"
          :max="descriptor.duration.max"
          :step="descriptor.duration.step"
          :append="descriptor.duration.unit || ''"
          :disabled="disabled"
          @update:model-value="onDurationMax"
        />
      </FormField>
    </template>

    <FormField v-for="p in shownParams" :key="p.key" :label="p.label" :id="`expr-param-${p.key}`">
      <div v-if="p.type === 'enum'" class="targets">
        <button
          v-for="opt in p.options"
          :key="opt.value"
          type="button"
          class="target"
          :class="{ active: paramValue(p) === opt.value }"
          :disabled="disabled"
          @click="emit('update', { [p.key]: opt.value })"
        >
          {{ opt.label }}
        </button>
      </div>
      <NumberSlider
        v-else
        :id="`expr-param-${p.key}`"
        :model-value="paramValue(p)"
        :min="p.min ?? 0"
        :max="paramMax(p)"
        :step="p.step ?? 1"
        :append="p.unit || ''"
        :disabled="disabled"
        @update:model-value="(value) => emit('update', { [p.key]: value })"
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
