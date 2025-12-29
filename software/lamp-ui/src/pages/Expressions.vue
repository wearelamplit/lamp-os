<script setup lang="ts">
import { ref, computed } from 'vue'
import ComponentForm from '@/components/Form.vue'
import type { FieldDefinition, FormValues } from '@/types'
import { useLampStore } from '@/stores/lamp'
import { useExpressionsStore } from '@/stores/expressions'

const lampStore = useLampStore()
const expressionsStore = useExpressionsStore()

// Track which expression panel is currently expanded (only one at a time)
const expandedPanel = ref<string | null>(null)

// Check if a specific panel is expanded
const isPanelExpanded = (expressionIndex: string): boolean => {
  return expandedPanel.value === expressionIndex
}

// Handle panel expansion - closes other panels
const handlePanelToggle = (expressionIndex: string, isOpen: boolean) => {
  if (isOpen) {
    expandedPanel.value = expressionIndex
  } else {
    expandedPanel.value = null
  }
}

// Get expression values from lamp state
const getExpressionValues = (expressionIndex: string): FormValues => {
  const expr = lampStore.state.expressions?.find((e) => e.type === expressionIndex)
  if (!expr) return {}

  const values: FormValues = {
    enabled: expr.enabled,
    target: expr.target,
    colors: expr.colors,
  }

  // Handle interval range fields
  if (expr.intervalMin !== undefined) values.intervalMin = expr.intervalMin
  if (expr.intervalMax !== undefined) values.intervalMax = expr.intervalMax

  // Handle duration range fields (glitchy)
  if (expr.durationMin !== undefined) values.durationMin = expr.durationMin
  if (expr.durationMax !== undefined) values.durationMax = expr.durationMax

  // Handle shift duration range fields (shifty)
  if (expr.shiftDurationMin !== undefined) values.shiftDurationMin = expr.shiftDurationMin
  if (expr.shiftDurationMax !== undefined) values.shiftDurationMax = expr.shiftDurationMax

  // Handle other fields
  if (expr.fadeDuration !== undefined) values.fadeDuration = expr.fadeDuration
  if (expr.pulseSpeed !== undefined) values.pulseSpeed = expr.pulseSpeed

  return values
}

// Check if an expression is enabled
const isExpressionEnabled = (expressionIndex: string): boolean => {
  const expr = lampStore.state.expressions?.find((e) => e.type === expressionIndex)
  return expr?.enabled ?? false
}

// Toggle expression enabled state
const toggleExpression = (expressionIndex: string, enabled: boolean) => {
  const expressions = [...(lampStore.state.expressions || [])]
  const existingIndex = expressions.findIndex((e) => e.type === expressionIndex)

  if (enabled) {
    if (existingIndex === -1) {
      // Add new expression with defaults from library
      const exprDef = expressionsStore.getExpression(expressionIndex)
      if (exprDef) {
        const defaultValues: Record<string, unknown> = {
          type: expressionIndex,
          enabled: true,
          target: 2,
          colors: ['#FFFFFFFF'],
        }

        // Apply defaults from field definitions
        exprDef.fields.forEach((field) => {
          if (field.default !== undefined) {
            if (field.transform && Array.isArray(field.transform)) {
              // Handle range fields - split array into Min/Max values
              const values = field.default as number[]
              field.transform.forEach((suffix, i) => {
                defaultValues[`${field.name}${suffix}`] = values[i]
              })
            } else {
              defaultValues[field.name as string] = field.default
            }
          }
        })

        expressions.push(defaultValues as typeof expressions[0])
      }
    } else {
      expressions[existingIndex].enabled = true
    }
  } else {
    if (existingIndex !== -1) {
      expressions[existingIndex].enabled = false
    }
  }

  lampStore.updateExpressions(expressions)
}

// Update expression values
const updateExpressionValues = (expressionIndex: string, values: FormValues) => {
  const expressions = [...(lampStore.state.expressions || [])]
  const existingIndex = expressions.findIndex((e) => e.type === expressionIndex)

  if (existingIndex !== -1) {
    const updated = { ...expressions[existingIndex] }

    // Update fields from form values
    Object.entries(values).forEach(([key, value]) => {
      if (key !== 'enabled') {
        (updated as Record<string, unknown>)[key] = value
      }
    })

    expressions[existingIndex] = updated
    lampStore.updateExpressions(expressions)
  }
}

// Get all available expression types
const allExpressions = computed(() => expressionsStore.expressionsList)

// Test expression
const handleTestExpression = (expressionIndex: string) => {
  lampStore.testExpression(expressionIndex)
}
</script>

<template>
  <section class="tab-panel" aria-label="Expression settings">
    <InfoPanel>
      Add expressions to give your lamp personality. Expressions are behaviors that trigger
      randomly to create visual effects.
    </InfoPanel>

    <div class="expressions-list">
      <CollapsiblePanel
        v-for="expr in allExpressions"
        :key="expr.index"
        :label="expr.name"
        :model-value="isPanelExpanded(expr.index)"
        @update:model-value="(v: boolean) => handlePanelToggle(expr.index, v)"
      >
        <template #left>
          <div class="expression-toggle" @click.stop>
            <button
              type="button"
              class="boolean-toggle"
              :class="{ 'boolean-toggle--active': isExpressionEnabled(expr.index) }"
              @click.stop="toggleExpression(expr.index, !isExpressionEnabled(expr.index))"
              :disabled="lampStore.disabled"
              :aria-checked="isExpressionEnabled(expr.index)"
              role="switch"
            >
              <div class="boolean-toggle-track">
                <div class="boolean-toggle-thumb"></div>
              </div>
            </button>
          </div>
        </template>

        <!-- Expression description -->
        <div class="expression-description">
          <p>{{ expr.description }}</p>
        </div>

        <!-- Expression form (only show when enabled) -->
        <div v-if="isExpressionEnabled(expr.index)" class="expression-form">
          <ComponentForm
            :fields="expr.fields"
            :model-value="getExpressionValues(expr.index)"
            @update:model-value="(values) => updateExpressionValues(expr.index, values)"
            :show-button="false"
            :disabled="lampStore.disabled"
          />

          <!-- Test button -->
          <div class="expression-actions">
            <button
              type="button"
              class="test-button"
              @click="handleTestExpression(expr.index)"
              :disabled="lampStore.disabled"
            >
              Test {{ expr.name }}
            </button>
          </div>
        </div>

        <InfoPanel v-else>
          Enable this expression to configure it.
        </InfoPanel>
      </CollapsiblePanel>
    </div>
  </section>
</template>

<style scoped>
.tab-panel {
  animation: fadeIn 0.3s ease-in-out;
}

@keyframes fadeIn {
  from {
    opacity: 0;
    transform: translateY(10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.expressions-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.expression-toggle {
  display: flex;
  align-items: center;
}

/* Boolean toggle styles (inline version) */
.boolean-toggle {
  position: relative;
  width: 44px;
  height: 24px;
  border: none;
  border-radius: 12px;
  background-color: var(--color-background-mute);
  cursor: pointer;
  transition: all 0.2s ease;
  padding: 0;
  display: flex;
  align-items: center;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
}

.boolean-toggle:focus {
  outline: none;
  box-shadow: 0 0 0 3px rgba(68, 108, 156, 0.2);
}

.boolean-toggle--active {
  background-color: var(--color-background-mute);
}

.boolean-toggle:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.boolean-toggle-track {
  position: relative;
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
}

.boolean-toggle-thumb {
  position: absolute;
  left: 2px;
  width: 20px;
  height: 20px;
  background-color: var(--brand-slate-grey);
  border-radius: 50%;
  transition: all 0.2s ease;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.3);
}

.boolean-toggle--active .boolean-toggle-thumb {
  left: calc(100% - 22px);
  background-color: var(--brand-lumen-green);
}

.expression-form {
  margin-top: 8px;
}

.expression-actions {
  margin-top: 16px;
  display: flex;
  justify-content: center;
}

.expression-description {
  margin-bottom: 20px;
  margin-top: 12px;
  padding: 0;
}

.expression-description p {
  margin: 0;
  font-size: 0.95rem;
  color: var(--brand-lumen-green);
  line-height: 1.6;
  font-style: italic;
}

.test-button {
  padding: 10px 20px;
  background: rgba(59, 130, 246, 0.1);
  color: #3b82f6;
  border: 2px solid #3b82f6;
  border-radius: 8px;
  font-size: 0.9rem;
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

</style>
