<!-- eslint-disable vue/multi-word-component-names -->
<script setup lang="ts">
import { ref } from 'vue'

interface Props {
  label?: string
  id?: string
  error?: string
  required?: boolean
  help?: string
  expandable?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  required: false,
  help: '',
  expandable: false,
})

const isExpanded = ref(false)

const toggleExpanded = () => {
  if (props.expandable) {
    isExpanded.value = !isExpanded.value
  }
}
</script>

<template>
  <div class="form-field" :class="{ 'form-field--error': error }">
    <label
      v-if="label"
      :for="id"
      :class="['form-field-label', { 'form-field-label-clickable': expandable }]"
      @click="toggleExpanded"
    >
      <span
        v-if="expandable"
        class="form-field-expand-icon"
        :class="{ 'form-field-expand-icon-expanded': isExpanded }"
      >
        ▶
      </span>
      {{ label }}
      <span v-if="required" class="form-field-required" aria-label="required">*</span>
    </label>

    <div v-if="!expandable || isExpanded" class="form-field-content">
      <slot />
    </div>

    <div v-if="error" class="form-field-error" role="alert">
      {{ error }}
    </div>

    <div v-else-if="help" class="form-field-help">
      {{ help }}
    </div>
  </div>
</template>

<style scoped>
.form-field {
  display: flex;
  flex-direction: column;
  gap: 10px;
  width: 100%;
  margin-bottom: 24px;
  margin-top: 8px;
}

.form-field--error .form-field-content :deep(input),
.form-field--error .form-field-content :deep(.boolean-input),
.form-field--error .form-field-content :deep(.number-slider) {
  border-color: var(--color-error);
}

.form-field-label {
  font-weight: 400;
  color: var(--brand-cloud-grey);
  font-size: 0.8rem;
  display: flex;
  align-items: center;
  gap: 6px;
  letter-spacing: -0.01em;
}

.form-field-label-clickable {
  cursor: pointer;
  user-select: none;
  transition: opacity 0.2s ease;
}

.form-field-label-clickable:hover {
  opacity: 0.8;
}

.form-field-expand-icon {
  font-size: 0.8rem;
  transition: transform 0.2s ease;
  margin-right: 4px;
}

.form-field-expand-icon-expanded {
  transform: rotate(90deg);
}

.form-field-required {
  color: var(--color-error);
  font-weight: 700;
}

.form-field-content {
  width: 100%;
}

.form-field-error {
  color: var(--color-error);
  font-size: 0.8rem;
  font-weight: 600;
  margin-top: 4px;
  display: flex;
  align-items: center;
  gap: 6px;
}

.form-field-error::before {
  content: '⚠';
  font-size: 0.8rem;
}

.form-field-help {
  margin-top: 6px;
  padding: 8px 12px;
  background: rgba(68, 108, 156, 0.08);
  border-left: 2px solid var(--brand-aurora-blue);
  border-radius: 4px;
  font-size: 0.75rem;
  line-height: 1.4;
  color: var(--brand-slate-grey);
}

/* Mobile optimizations */
@media (max-width: 768px) {
  .form-field-label {
    font-size: 0.8rem;
  }

  .form-field-error {
    font-size: 0.75rem;
  }

  .form-field-help {
    font-size: 0.7rem;
    padding: 6px 10px;
  }
}

@media (max-width: 480px) {
  .form-field {
    gap: 8px;
  }

  .form-field-label {
    font-size: 0.8rem;
  }
}
</style>
