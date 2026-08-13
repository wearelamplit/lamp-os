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
  preview: [colors: string[], target: number]
  'preview-end': []
  'test-expression': [type: string, target: number]
}>()

const byId = computed(() => new Map(props.catalog.map((d) => [d.id, d])))

const expandedIndex = ref<number | null>(null)
const showAddModal = ref(false)

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
  if (!d) return

  // New instances default to the base surface (target 2).
  const expr: Expression = {
    type,
    enabled: true,
    target: 2,
    intervalMin: d.interval?.default[0] ?? 300,
    intervalMax: d.interval?.default[1] ?? 900,
    colors: d.colors.inheritsSurface ? [] : ['#FF000000'],
  }
  if (d.duration?.minKey) expr[d.duration.minKey] = d.duration.default[0]
  if (d.duration?.maxKey) expr[d.duration.maxKey] = d.duration.default[1]
  for (const p of d.params ?? []) {
    expr[p.key] = resolveBound(p.default, props.basePx, p.min ?? 0)
  }

  update([...props.modelValue, expr])
  showAddModal.value = false
  expandedIndex.value = props.modelValue.length
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
          type="button"
          class="test"
          :disabled="disabled"
          @click="emit('test-expression', expr.type, expr.target)"
        >
          Test
        </button>
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
          @preview="(colors) => emit('preview', colors, expr.target)"
          @preview-end="emit('preview-end')"
        />
      </div>
    </div>

    <button
      v-if="catalog.length > 0"
      type="button"
      class="add"
      :disabled="disabled"
      @click="showAddModal = true"
    >
      + Add expression
    </button>

    <div v-if="showAddModal" class="modal-overlay" @click="showAddModal = false">
      <div class="modal-container" @click.stop>
        <div class="modal-box">
          <h3>Add an expression</h3>

          <div class="type-list">
            <button
              v-for="t in catalog"
              :key="t.id"
              type="button"
              class="type"
              :disabled="disabled"
              @click="addExpression(t.id)"
            >
              <span class="type-name">{{ t.name }}</span>
            </button>
          </div>

          <div class="modal-actions">
            <button type="button" class="cancel" @click="showAddModal = false">Cancel</button>
          </div>
        </div>
      </div>
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

.test {
  padding: 6px 12px;
  background: rgba(68, 108, 156, 0.15);
  color: var(--brand-aurora-blue);
  border: 1px solid rgba(68, 108, 156, 0.5);
  border-radius: 4px;
  cursor: pointer;
}

.test:disabled {
  opacity: 0.5;
  cursor: not-allowed;
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

.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.8);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 2000;
}

.modal-container {
  width: 100%;
  max-width: 500px;
  padding: 20px;
}

.modal-box {
  background: var(--color-background-soft);
  border-radius: 16px;
  padding: 32px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
}

.modal-box h3 {
  color: var(--brand-lamp-white);
  margin: 0 0 24px 0;
  font-size: 1.5rem;
  font-weight: 600;
}

.type-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-bottom: 24px;
}

.type {
  text-align: left;
  padding: 16px;
  background: rgba(255, 255, 255, 0.05);
  color: var(--brand-fog-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.2s;
}

.type:hover:not(:disabled) {
  background: rgba(255, 255, 255, 0.1);
  border-color: var(--brand-lumen-green);
}

.type:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.type-name {
  display: block;
  font-weight: 500;
}

.modal-actions {
  display: flex;
  justify-content: flex-end;
}

.cancel {
  padding: 8px 16px;
  background: transparent;
  color: var(--brand-light-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 6px;
  cursor: pointer;
}

.cancel:hover {
  background: rgba(255, 255, 255, 0.05);
}
</style>
