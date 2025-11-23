<template>
  <div class="expressions-container">
    <div v-if="expressions.length === 0" class="empty-state">
      <p>No expressions configured</p>
      <p class="empty-state-hint">Add an expression to give your lamp personality</p>
    </div>

    <div v-else class="expressions-list">
      <div v-for="(expr, index) in expressions" :key="index" class="expression-item">
        <div class="expression-header">
          <BooleanInput
            :model-value="expr.enabled"
            @update:model-value="(value) => updateExpression(index, { enabled: value })"
            :disabled="disabled"
          />

          <span class="expression-name">{{ getExpressionName(expr.type) }}</span>

          <div class="expression-actions">
            <button
              class="config-button"
              @click="toggleConfig(index)"
              :disabled="disabled"
              :aria-expanded="expandedIndex === index"
            >
              {{ expandedIndex === index ? 'Hide' : 'Configure' }}
            </button>
            <button
              class="delete-button"
              @click="removeExpression(index)"
              :disabled="disabled"
              aria-label="Delete expression"
            >
              ×
            </button>
          </div>
        </div>

        <!-- Inline configuration panel -->
        <Transition name="expand">
          <div v-if="expandedIndex === index" class="expression-config">
            <ExpressionConfig
              :expression="expr"
              :config-schema="getConfigSchema(expr.type)"
              @update="(updates) => updateExpression(index, updates)"
              @preview-color="(color, target) => emit('preview-color', color, target)"
              @color-picker-open="() => emit('color-picker-open')"
              @color-picker-close="() => emit('color-picker-close')"
              :disabled="disabled"
            />
          </div>
        </Transition>
      </div>
    </div>

    <!-- Expression Action Buttons -->
    <div class="expression-actions-container">
      <button
        class="test-button"
        @click="handleTestExpressions"
        :disabled="disabled"
      >
        Test Expressions
      </button>
      <button
        class="add-button"
        @click="showAddModal = true"
        :disabled="disabled"
      >
        + Add Expression
      </button>
    </div>

    <!-- Add Expression Modal -->
    <div v-if="showAddModal" class="modal-overlay" @click="closeModal">
      <div class="modal-container" @click.stop>
        <div class="modal-box">
          <h3>Add Expression</h3>
          <p class="modal-description">Choose an expression type to add to your lamp:</p>

          <div class="expression-types">
            <button
              v-for="type in availableTypes"
              :key="type.id"
              @click="addExpression(type.id)"
              class="expression-type-button"
              :class="{ 'already-added': type.isAlreadyAdded }"
              :disabled="disabled || type.isAlreadyAdded"
            >
              <span class="expression-type-name">
                {{ type.name }}
                <span v-if="type.isAlreadyAdded" class="already-added-indicator">✓ Added</span>
              </span>
              <span class="expression-type-desc">{{ type.description }}</span>
            </button>
          </div>

          <div class="modal-actions">
            <button @click="closeModal" class="cancel-button">Cancel</button>
          </div>
        </div>
      </div>
    </div>

    <!-- Test Expressions Modal -->
    <div v-if="showTestModal" class="modal-overlay" @click="closeModal">
      <div class="modal-container" @click.stop>
        <div class="modal-box">
          <h3>Test Expressions</h3>
          <p class="modal-description">Trigger individual expressions to test their behavior:</p>

          <div v-if="expressions.length === 0" class="no-expressions">
            <p>No expressions configured yet.</p>
          </div>

          <div v-else class="test-expressions-list">
            <div v-for="(expr, index) in expressions" :key="index" class="test-expression-item">
              <div class="test-expression-info">
                <span class="test-expression-name">{{ getExpressionName(expr.type) }}</span>
                <span class="test-expression-status" :class="{ enabled: expr.enabled, disabled: !expr.enabled }">
                  {{ expr.enabled ? 'Enabled' : 'Disabled' }}
                </span>
                <span class="test-expression-target">{{ getTargetLabel(expr.target) }}</span>
              </div>
              <button
                class="test-expression-button"
                @click="testExpression(expr)"
                :disabled="disabled || !expr.enabled"
              >
                Test
              </button>
            </div>
          </div>

          <div class="modal-actions">
            <button @click="closeModal" class="cancel-button">Close</button>
          </div>
        </div>
      </div>
    </div>

    <!-- Unsaved Changes Prompt Modal -->
    <div v-if="showUnsavedPrompt" class="modal-overlay" @click="closeModal">
      <div class="modal-container" @click.stop>
        <div class="modal-box">
          <h3>Unsaved Changes</h3>
          <p class="modal-description">You have unsaved expression changes. Please save and restart first to test with current configuration.</p>

          <div class="modal-actions">
            <button @click="closeModal" class="cancel-button">Cancel</button>
            <button @click="saveAndRestart" class="save-restart-button">Save & Restart</button>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import BooleanInput from '@/components/BooleanInput.vue'
import ExpressionConfig from './ExpressionConfig.vue'
import expressionSchemas from '@/assets/expressions.json'

interface Expression {
  type: string
  enabled: boolean
  colors: string[]
  intervalMin: number
  intervalMax: number
  target: number
  durationMin?: number
  durationMax?: number
  fadeDuration?: number
  shiftDurationMin?: number
  shiftDurationMax?: number
  pulseSpeed?: number
}

const props = defineProps<{
  modelValue: Expression[]
  disabled?: boolean
  // Add a prop to reset unsaved changes when save succeeds
  resetUnsavedChanges?: number
}>()

const emit = defineEmits<{
  'update:modelValue': [value: Expression[]]
  'test-expression': [type: string]
  'test-expression-complete': []
  'save-and-restart': []
  'preview-color': [color: string, target: number]
  'color-picker-open': []
  'color-picker-close': []
}>()

const expandedIndex = ref<number | null>(null)
const showAddModal = ref(false)
const showTestModal = ref(false)
const showUnsavedPrompt = ref(false)
const hasUnsavedChanges = ref(false)

const expressions = computed({
  get: () => props.modelValue || [],
  set: (value) => {
    hasUnsavedChanges.value = true
    emit('update:modelValue', value)
  }
})

const existingTypes = computed(() => new Set(expressions.value.map(expr => expr.type)))

const availableTypes = computed(() => {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  return Object.entries(expressionSchemas.expressions).map(([id, config]: [string, any]) => ({
    id,
    name: config.name,
    description: config.description,
    isAlreadyAdded: existingTypes.value.has(id)
  }))
})

const getExpressionName = (type: string): string => {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  return (expressionSchemas.expressions as any)[type]?.name || type
}

const getTargetLabel = (target: number): string => {
  switch (target) {
    case 1: return 'Shade'
    case 2: return 'Base'
    case 3: return 'Both'
    default: return 'Unknown'
  }
}

const getConfigSchema = (type: string) => {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  return (expressionSchemas.expressions as any)[type]?.config || {}
}

const toggleConfig = (index: number) => {
  expandedIndex.value = expandedIndex.value === index ? null : index
}

const updateExpression = (index: number, updates: Partial<Expression>) => {
  const newExpressions = [...expressions.value]
  newExpressions[index] = { ...newExpressions[index], ...updates }
  expressions.value = newExpressions
}

const handleTestExpressions = () => {
  if (hasUnsavedChanges.value) {
    showUnsavedPrompt.value = true
  } else {
    showTestModal.value = true
  }
}

const testExpression = (expression: Expression) => {
  emit('test-expression', expression.type)
}

const saveAndRestart = () => {
  emit('save-and-restart')
  showUnsavedPrompt.value = false
}

const addExpression = (type: string) => {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const schema = (expressionSchemas.expressions as any)[type]
  if (!schema) return

  // Don't add if already exists
  if (existingTypes.value.has(type)) return

  // Build expression with only fields that exist in this type's config
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const newExpression: any = {
    type,
    enabled: true,
    target: 2, // Default to base
  }

  // Add fields based on what's defined in the JSON config
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  Object.entries(schema.config).forEach(([key, config]: [string, any]) => {
    if (config.default !== undefined) {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (newExpression as any)[key] = config.default
    }
  })

  // Ensure colors array exists
  if (!newExpression.colors) {
    newExpression.colors = ['#FFFFFFFF']
  }

  // Set default intervals if not specified
  if (!newExpression.intervalMin) newExpression.intervalMin = 300
  if (!newExpression.intervalMax) newExpression.intervalMax = 900

  expressions.value = [...expressions.value, newExpression as Expression]
  showAddModal.value = false
}

const closeModal = () => {
  showAddModal.value = false

  // If closing test modal, notify that test is complete
  if (showTestModal.value) {
    emit('test-expression-complete')
  }

  showTestModal.value = false
  showUnsavedPrompt.value = false
}

const removeExpression = (index: number) => {
  expressions.value = expressions.value.filter((_, i) => i !== index)
  if (expandedIndex.value === index) {
    expandedIndex.value = null
  }
}

// Watch for reset signal from parent component
watch(() => props.resetUnsavedChanges, () => {
  hasUnsavedChanges.value = false
})
</script>

<style scoped>
.expressions-container {
  padding: 0;
}

.empty-state {
  text-align: center;
  padding: 40px 20px;
  color: var(--brand-fog-grey);
}

.empty-state-hint {
  font-size: 0.875rem;
  color: var(--brand-light-grey);
  margin-top: 8px;
}

.expressions-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-bottom: 20px;
}

.expression-item {
  background: rgba(255, 255, 255, 0.05);
  border-radius: 8px;
  overflow: hidden;
}

.expression-header {
  display: flex;
  align-items: center;
  padding: 12px;
  gap: 12px;
}

.expression-name {
  font-weight: 500;
  color: var(--brand-fog-grey);
  font-size: 1rem;
  flex: 1;
  display: block;
  padding-left: 0;
  margin-left: 0;
}

.expression-actions {
  display: flex;
  gap: 8px;
  flex-shrink: 0;
}

.config-button {
  padding: 6px 12px;
  background: rgba(64, 176, 0, 0.15);
  color: var(--brand-green);
  border: 1px solid rgba(64, 176, 0, 0.5);
  border-radius: 4px;
  font-size: 0.813rem;
  cursor: pointer;
  transition: all 0.2s;
  white-space: nowrap;
}

.config-button:hover:not(:disabled) {
  background: rgba(64, 176, 0, 0.3);
}

.config-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.delete-button {
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
  display: flex;
  align-items: center;
  justify-content: center;
}

.delete-button:hover:not(:disabled) {
  background: rgba(239, 68, 68, 0.3);
}

.delete-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.expression-config {
  padding: 0 12px 12px;
  border-top: 1px solid rgba(255, 255, 255, 0.1);
}

.expression-actions-container {
  margin-top: 20px;
  display: flex;
  gap: 12px;
}

.test-button {
  flex: 1;
  padding: 12px;
  background: rgba(59, 130, 246, 0.1);
  color: #3b82f6;
  border: 2px solid #3b82f6;
  border-radius: 8px;
  font-size: 1rem;
  cursor: pointer;
  transition: all 0.2s;
}

.test-button:hover:not(:disabled) {
  background: rgba(59, 130, 246, 0.2);
}

.test-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.add-button {
  flex: 1;
  padding: 12px;
  background: rgba(64, 176, 0, 0.1);
  color: var(--brand-green);
  border: 2px dashed var(--brand-green);
  border-radius: 8px;
  font-size: 1rem;
  cursor: pointer;
  transition: all 0.2s;
}

.add-button:hover:not(:disabled) {
  background: rgba(64, 176, 0, 0.2);
}

.add-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

/* Modal Styles */
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
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
  margin: 0 0 8px 0;
  font-size: 1.5rem;
  font-weight: 600;
}

.modal-description {
  color: var(--brand-light-grey);
  margin: 0 0 24px 0;
  font-size: 0.9rem;
}

.expression-types {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-bottom: 24px;
}

.expression-type-button {
  width: 100%;
  padding: 16px;
  background: rgba(255, 255, 255, 0.05);
  color: var(--brand-fog-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 8px;
  text-align: left;
  cursor: pointer;
  transition: all 0.2s;
}

.expression-type-button:hover:not(:disabled) {
  background: rgba(255, 255, 255, 0.1);
  border-color: var(--brand-green);
}

.expression-type-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.expression-type-button.already-added {
  background: rgba(255, 255, 255, 0.02);
  border-color: rgba(255, 255, 255, 0.1);
  opacity: 0.6;
}

.expression-type-button.already-added:hover {
  background: rgba(255, 255, 255, 0.02);
  border-color: rgba(255, 255, 255, 0.1);
}

.expression-type-name {
  display: block;
  font-weight: 500;
  margin-bottom: 4px;
  color: var(--brand-white);
}

.already-added-indicator {
  font-size: 0.8rem;
  color: var(--brand-green);
  font-weight: 400;
  margin-left: 8px;
}

.expression-type-desc {
  display: block;
  font-size: 0.85rem;
  color: var(--brand-light-grey);
}

.modal-actions {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
}

.cancel-button {
  padding: 8px 16px;
  background: transparent;
  color: var(--brand-light-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s;
}

.cancel-button:hover {
  background: rgba(255, 255, 255, 0.05);
}

/* Test Expressions Modal */
.no-expressions {
  text-align: center;
  padding: 20px;
  color: var(--brand-light-grey);
}

.test-expressions-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-bottom: 24px;
}

.test-expression-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 8px;
}

.test-expression-info {
  display: flex;
  flex-direction: column;
  gap: 4px;
  flex: 1;
}

.test-expression-name {
  font-weight: 500;
  color: var(--brand-white);
  font-size: 1rem;
}

.test-expression-status {
  font-size: 0.875rem;
  font-weight: 500;
}

.test-expression-status.enabled {
  color: var(--brand-green);
}

.test-expression-status.disabled {
  color: #ef4444;
}

.test-expression-target {
  font-size: 0.8rem;
  color: var(--brand-light-grey);
}

.test-expression-button {
  padding: 8px 16px;
  background: rgba(59, 130, 246, 0.2);
  color: #3b82f6;
  border: 1px solid #3b82f6;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s;
  font-size: 0.875rem;
}

.test-expression-button:hover:not(:disabled) {
  background: rgba(59, 130, 246, 0.3);
}

.test-expression-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.save-restart-button {
  padding: 8px 16px;
  background: var(--brand-green);
  color: var(--brand-dark-grey);
  border: 1px solid var(--brand-green);
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s;
  font-weight: 500;
}

.save-restart-button:hover {
  background: rgba(64, 176, 0, 0.8);
}

/* Transitions */
.expand-enter-active,
.expand-leave-active {
  transition: all 0.3s ease;
  max-height: 500px;
  overflow: hidden;
}

.expand-enter-from,
.expand-leave-to {
  max-height: 0;
  opacity: 0;
  transform: translateY(-10px);
}

</style>