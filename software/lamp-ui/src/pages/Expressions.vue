<script setup lang="ts">
import ExpressionsList from '@/components/expressions/ExpressionsList.vue'
import { useLampState } from '@/composables/useLampState'

const {
  settings,
  disabled,
  resetUnsavedChanges,
  updateSetting,
  handleTestExpression,
  handleTestExpressionComplete,
  handleExpressionColorPreview,
  handleExpressionColorPickerClose,
  handleSaveAndRestart,
} = useLampState()

const handleExpressionColorPickerOpen = () => {
  // Configurator stays enabled, expression color will override temporarily
}
</script>

<template>
  <section class="tab-panel" aria-label="Expression settings">
    <div class="expressions-instructions">
      <p>
        Add expressions to give your lamp personality. Expressions are behaviors that trigger
        randomly to create visual effects.
      </p>
    </div>
    <ExpressionsList
      :model-value="settings.expressions || []"
      @update:model-value="(value) => updateSetting('expressions', value)"
      @test-expression="handleTestExpression"
      @test-expression-complete="handleTestExpressionComplete"
      @save-and-restart="handleSaveAndRestart"
      @preview-color="handleExpressionColorPreview"
      @color-picker-open="handleExpressionColorPickerOpen"
      @color-picker-close="handleExpressionColorPickerClose"
      :reset-unsaved-changes="resetUnsavedChanges"
      :disabled="disabled"
    />
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

.expressions-instructions {
  margin-bottom: 24px;
  padding: 12px 16px;
  background: rgba(68, 108, 156, 0.06);
  border-radius: 6px;
}

.expressions-instructions p {
  margin: 0;
  font-size: 0.85rem;
  line-height: 1.5;
  color: var(--brand-fog-grey);
}
</style>

