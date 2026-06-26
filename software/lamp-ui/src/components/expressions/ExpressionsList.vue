<script setup lang="ts">
import { computed, ref } from 'vue'
import BooleanInput from '../BooleanInput.vue'
import ExpressionConfig from './ExpressionConfig.vue'
import schemas from '../../assets/expressions.json'
import type { Expression } from '../../types'

const props = withDefaults(
  defineProps<{ modelValue: Expression[]; disabled?: boolean }>(),
  { disabled: false },
)

const emit = defineEmits<{
  'update:modelValue': [value: Expression[]]
  preview: [colors: string[]]
}>()

type SchemaEntry = {
  name: string
  description: string
  config: Record<string, { type: string; max?: number; default?: unknown }>
}
const expressionSchemas = schemas.expressions as Record<string, SchemaEntry>

const expandedIndex = ref<number | null>(null)
const showAdd = ref(false)

const existingTypes = computed(() => new Set(props.modelValue.map((e) => e.type)))

const availableTypes = computed(() =>
  Object.entries(expressionSchemas).map(([id, s]) => ({
    id,
    name: s.name,
    description: s.description,
    added: existingTypes.value.has(id),
  })),
)

const nameOf = (type: string) => expressionSchemas[type]?.name ?? type
const maxColorsFor = (type: string) => expressionSchemas[type]?.config?.colors?.max ?? 5

const update = (next: Expression[]) => emit('update:modelValue', next)

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

const addExpression = (type: string) => {
  const schema = expressionSchemas[type]
  if (!schema || existingTypes.value.has(type)) return

  // Seed every field the schema defines so per-type params (durationMin,
  // pulseSpeed, …) exist for the firmware even though we don't render them.
  const expr: Expression = {
    type,
    enabled: true,
    target: 2,
    intervalMin: 300,
    intervalMax: 900,
    colors: ['#FF0000FF'],
  }
  for (const [key, cfg] of Object.entries(schema.config)) {
    if (cfg.default !== undefined) expr[key] = cfg.default
  }
  if (!Array.isArray(expr.colors) || expr.colors.length === 0) {
    expr.colors = ['#FF0000FF']
  }

  update([...props.modelValue, expr])
  showAdd.value = false
}
</script>

<template>
  <div class="expressions">
    <p v-if="modelValue.length === 0" class="empty">No expressions yet.</p>

    <div v-for="(expr, index) in modelValue" :key="index" class="item">
      <div class="header">
        <BooleanInput
          :model-value="expr.enabled"
          :disabled="disabled"
          @update:model-value="(value) => updateExpression(index, { enabled: value })"
        />
        <span class="name">{{ nameOf(expr.type) }}</span>
        <button type="button" class="cfg" :disabled="disabled" @click="toggleConfig(index)">
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

      <div v-if="expandedIndex === index" class="body">
        <ExpressionConfig
          :expression="expr"
          :max-colors="maxColorsFor(expr.type)"
          :disabled="disabled"
          @update="(updates) => updateExpression(index, updates)"
          @preview="(colors) => emit('preview', colors)"
        />
      </div>
    </div>

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
        <span class="type-desc">{{ t.description }}</span>
      </button>
      <button type="button" class="cancel" @click="showAdd = false">Cancel</button>
    </div>
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

.type-desc {
  display: block;
  font-size: 0.85rem;
  color: var(--brand-slate-grey);
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
