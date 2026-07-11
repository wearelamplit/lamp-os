<script setup lang="ts">
import { computed, ref } from 'vue'
import BooleanInput from '../BooleanInput.vue'
import ExpressionConfig from './ExpressionConfig.vue'
import { resolveBound } from './catalog'
import type { Expression, ExpressionDescriptor } from '../../types'

const props = withDefaults(
  defineProps<{
    modelValue: Expression[]
    catalog: ExpressionDescriptor[]
    basePx: number
    shadePx: number
    disabled?: boolean
  }>(),
  { disabled: false },
)

const emit = defineEmits<{
  'update:modelValue': [value: Expression[]]
  preview: [colors: string[]]
}>()

const byId = computed(() => new Map(props.catalog.map((d) => [d.id, d])))

const expandedIndex = ref<number | null>(null)
const showAdd = ref(false)

const existingTypes = computed(() => new Set(props.modelValue.map((e) => e.type)))

const availableTypes = computed(() =>
  props.catalog.map((d) => ({
    id: d.id,
    name: d.name,
    added: existingTypes.value.has(d.id),
  })),
)

const nameOf = (type: string) => byId.value.get(type)?.name ?? type
const descriptorOf = (type: string) => byId.value.get(type)

const update = (next: Expression[]) => emit('update:modelValue', next)

// Overlay only the changed keys onto the existing instance so any key the web
// UI never renders (zones, per-type params the app set) survives the save.
const updateExpression = (index: number, updates: Partial<Expression>) => {
  const next = [...props.modelValue]
  next[index] = { ...next[index], ...updates }
  update(next)
}

const removeExpression = (index: number) => {
  update(props.modelValue.filter((_, i) => i !== index))
  if (expandedIndex.value === index) expandedIndex.value = null
}

const toggleConfig = (index: number) => {
  expandedIndex.value = expandedIndex.value === index ? null : index
}

// Seed every catalog default so per-type params (durationMin, pulseSpeed, …)
// exist for the firmware even though the web UI doesn't render them all.
const addExpression = (type: string) => {
  const d = byId.value.get(type)
  if (!d || existingTypes.value.has(type)) return

  // New instances default to the base surface (target 2).
  const expr: Expression = {
    type,
    enabled: true,
    target: 2,
    intervalMin: d.interval?.default[0] ?? 300,
    intervalMax: d.interval?.default[1] ?? 900,
    colors: d.colors.inheritsSurface ? [] : ['#FF0000FF'],
  }
  if (d.duration?.minKey) expr[d.duration.minKey] = d.duration.default[0]
  if (d.duration?.maxKey) expr[d.duration.maxKey] = d.duration.default[1]
  for (const p of d.params ?? []) {
    expr[p.key] = resolveBound(p.default, props.basePx, p.min ?? 0)
  }

  update([...props.modelValue, expr])
  showAdd.value = false
}
</script>

<template>
  <div class="expressions">
    <p v-if="catalog.length === 0" class="empty">
      Expression configuration isn't available on this firmware.
    </p>
    <p v-else-if="modelValue.length === 0" class="empty">No expressions yet.</p>

    <div v-for="(expr, index) in modelValue" :key="index" class="item">
      <div class="header">
        <BooleanInput
          :model-value="expr.enabled"
          :disabled="disabled"
          @update:model-value="(value) => updateExpression(index, { enabled: value })"
        />
        <span class="name">{{ nameOf(expr.type) }}</span>
        <button
          v-if="descriptorOf(expr.type)"
          type="button"
          class="cfg"
          :disabled="disabled"
          @click="toggleConfig(index)"
        >
          {{ expandedIndex === index ? 'Hide' : 'Configure' }}
        </button>
        <button
          type="button"
          class="del"
          :disabled="disabled"
          aria-label="Delete expression"
          @click="removeExpression(index)"
        >
          ×
        </button>
      </div>

      <div v-if="expandedIndex === index && descriptorOf(expr.type)" class="body">
        <ExpressionConfig
          :expression="expr"
          :descriptor="descriptorOf(expr.type)!"
          :base-px="basePx"
          :shade-px="shadePx"
          :disabled="disabled"
          @update="(updates) => updateExpression(index, updates)"
          @preview="(colors) => emit('preview', colors)"
        />
      </div>
    </div>

    <template v-if="catalog.length > 0">
      <button v-if="!showAdd" type="button" class="add" :disabled="disabled" @click="showAdd = true">
        + Add expression
      </button>

      <div v-else class="picker">
        <button
          v-for="t in availableTypes"
          :key="t.id"
          type="button"
          class="type"
          :disabled="disabled || t.added"
          @click="addExpression(t.id)"
        >
          <span class="type-name">{{ t.name }}<span v-if="t.added"> ✓</span></span>
        </button>
        <button type="button" class="cancel" @click="showAdd = false">Cancel</button>
      </div>
    </template>
  </div>
</template>

<style scoped>
.expressions {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.empty {
  color: var(--brand-slate-grey);
  font-style: italic;
}

.item {
  background: rgba(255, 255, 255, 0.05);
  border-radius: 8px;
  overflow: hidden;
}

.header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px;
}

.name {
  flex: 1;
  font-weight: 500;
  color: var(--brand-fog-grey);
}

.cfg {
  padding: 6px 12px;
  background: rgba(64, 176, 0, 0.15);
  color: var(--brand-lumen-green);
  border: 1px solid rgba(64, 176, 0, 0.5);
  border-radius: 4px;
  cursor: pointer;
}

.del {
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

.body {
  padding: 0 12px 12px;
  border-top: 1px solid rgba(255, 255, 255, 0.1);
}

.add {
  padding: 12px;
  background: rgba(64, 176, 0, 0.1);
  color: var(--brand-lumen-green);
  border: 2px dashed var(--brand-lumen-green);
  border-radius: 8px;
  cursor: pointer;
}

.picker {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.type {
  text-align: left;
  padding: 12px;
  background: rgba(255, 255, 255, 0.05);
  color: var(--brand-fog-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 8px;
  cursor: pointer;
}

.type:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.type-name {
  display: block;
  font-weight: 500;
}

.cancel {
  align-self: flex-start;
  padding: 8px 16px;
  background: transparent;
  color: var(--brand-light-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 6px;
  cursor: pointer;
}
</style>
