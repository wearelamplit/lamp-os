<script setup lang="ts">
// Renders the subset of the catalog the web UI owns: colors, interval,
// duration, int sliders (honoring invert + end labels), and enum selects.
// Zones and requiresZoning params are skipped; ExpressionsList round-trips
// every un-rendered instance key untouched.
import { computed } from 'vue'
import FormField from '../FormField.vue'
import ColorPicker from '../ColorPicker.vue'
import NumberSlider from '../NumberSlider.vue'
import RangeSlider from '../RangeSlider.vue'
import { resolveBound } from './catalog'
import { formatDuration } from '../../utils/duration'
import type { Expression, ExpressionDescriptor, CatalogParam, CatalogEnumOption } from '../../types'

// Catalog units are 's' or 'ms' (raw slider value in that unit); anything
// else (%, unitless) keeps the slider's default `value + append` label.
const timeFormat = (unit?: string) => {
  if (unit === 's') return (v: number) => formatDuration(v * 1000)
  if (unit === 'ms') return (v: number) => formatDuration(v)
  return undefined
}

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
  'preview-end': []
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

const optionGroups = (p: CatalogParam): { name: string | null; options: CatalogEnumOption[] }[] => {
  const opts = p.options ?? []
  if (!opts.some((o) => o.group)) return [{ name: null, options: opts }]
  const groups: { name: string | null; options: CatalogEnumOption[] }[] = []
  for (const o of opts) {
    const name = o.group ?? ''
    const last = groups[groups.length - 1]
    if (last && last.name === name) last.options.push(o)
    else groups.push({ name, options: [o] })
  }
  return groups
}

const paramMax = (p: CatalogParam) => resolveBound(p.max, pixelCount.value, p.min ?? 0)
const paramValue = (p: CatalogParam) => Number(props.expression[p.key] ?? p.min ?? 0)

// invert flips the slider axis for display only; the stored value stays raw so
// a "slow→fast" param reads left-to-right without changing the wire value.
const paramSliderValue = (p: CatalogParam) => {
  const v = paramValue(p)
  return p.invert ? (p.min ?? 0) + paramMax(p) - v : v
}
const onParamSlider = (p: CatalogParam, value: number) => {
  const v = p.invert ? (p.min ?? 0) + paramMax(p) - value : value
  emit('update', { [p.key]: v })
}

const updateColor = (index: number, value: string) => {
  const colors = [...props.expression.colors]
  colors[index] = value
  emit('update', { colors })
  emit('preview', colors)
}

const addColor = () => {
  const colors = [...props.expression.colors, '#FF000000']
  emit('update', { colors })
  emit('preview', colors)
}

const removeColor = (index: number) => {
  const colors = props.expression.colors.filter((_, i) => i !== index)
  emit('update', { colors })
  emit('preview', colors)
}

const onIntervalMin = (value: number) => {
  const gap = props.descriptor.interval?.minGap ?? 0
  const cap = props.descriptor.interval?.max ?? Number.MAX_SAFE_INTEGER
  const intervalMax = Math.min(cap, Math.max(value + gap, props.expression.intervalMax))
  emit('update', { intervalMin: Math.min(value, intervalMax - gap), intervalMax })
}

const onIntervalMax = (value: number) => {
  const gap = props.descriptor.interval?.minGap ?? 0
  const floor = props.descriptor.interval?.min ?? 0
  const intervalMin = Math.max(floor, Math.min(value - gap, props.expression.intervalMin))
  emit('update', { intervalMin, intervalMax: Math.max(value, intervalMin + gap) })
}

const onDurationMin = (value: number) => {
  const d = props.descriptor.duration
  if (!d?.minKey || !d.maxKey) return
  const gap = d.minGap ?? 0
  const hi = Math.min(d.max, Math.max(value + gap, Number(props.expression[d.maxKey] ?? value)))
  emit('update', { [d.minKey]: Math.min(value, hi - gap), [d.maxKey]: hi })
}

const onDurationMax = (value: number) => {
  const d = props.descriptor.duration
  if (!d?.minKey || !d.maxKey) return
  const gap = d.minGap ?? 0
  const lo = Math.max(d.min, Math.min(value - gap, Number(props.expression[d.minKey] ?? value)))
  emit('update', { [d.minKey]: lo, [d.maxKey]: Math.max(value, lo + gap) })
}
</script>

<template>
  <div class="config">
    <FormField
      label="Target"
      id="expr-target"
      help-text="Which part of the lamp shows this effect."
    >
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
            @close="emit('preview-end')"
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
      <FormField :label="descriptor.interval.label || 'Trigger interval'" id="expr-interval-min">
        <RangeSlider
          id="expr-interval"
          :min-value="expression.intervalMin"
          :max-value="expression.intervalMax"
          :min="descriptor.interval.min"
          :max="descriptor.interval.max"
          :step="descriptor.interval.step"
          :append="descriptor.interval.unit || ''"
          :format="timeFormat(descriptor.interval.unit)"
          :disabled="disabled"
          @update:min-value="onIntervalMin"
          @update:max-value="onIntervalMax"
        />
      </FormField>
    </template>

    <template v-if="descriptor.duration?.minKey && descriptor.duration?.maxKey">
      <FormField :label="descriptor.duration.label || 'Duration'" id="expr-duration-min">
        <RangeSlider
          id="expr-duration"
          :min-value="Number(expression[descriptor.duration.minKey] ?? descriptor.duration.default[0])"
          :max-value="Number(expression[descriptor.duration.maxKey] ?? descriptor.duration.default[1])"
          :min="descriptor.duration.min"
          :max="descriptor.duration.max"
          :step="descriptor.duration.step"
          :append="descriptor.duration.unit || ''"
          :format="timeFormat(descriptor.duration.unit)"
          :disabled="disabled"
          @update:min-value="onDurationMin"
          @update:max-value="onDurationMax"
        />
      </FormField>
    </template>

    <FormField v-for="p in shownParams" :key="p.key" :label="p.label" :id="`expr-param-${p.key}`" :help-text="p.help">
      <div v-if="p.type === 'enum'" class="enum-groups">
        <div v-for="g in optionGroups(p)" :key="g.name ?? '_flat'" class="enum-group">
          <span v-if="g.name" class="enum-group-label">{{ g.name }}</span>
          <div class="targets">
            <button
              v-for="opt in g.options"
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
        </div>
      </div>
      <div v-else class="slider-labelled">
        <NumberSlider
          :id="`expr-param-${p.key}`"
          :model-value="paramSliderValue(p)"
          :min="p.min ?? 0"
          :max="paramMax(p)"
          :step="p.step ?? 1"
          :append="p.unit || ''"
          :format="timeFormat(p.unit)"
          :disabled="disabled"
          @update:model-value="(value) => onParamSlider(p, value)"
        />
        <div v-if="p.leftLabel || p.rightLabel" class="slider-ends">
          <span>{{ p.leftLabel }}</span>
          <span>{{ p.rightLabel }}</span>
        </div>
      </div>
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

.enum-groups {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.enum-group-label {
  font-size: 0.7rem;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--brand-slate-grey);
}

.slider-labelled {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.slider-ends {
  display: flex;
  justify-content: space-between;
  font-size: 0.7rem;
  color: var(--brand-slate-grey);
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
