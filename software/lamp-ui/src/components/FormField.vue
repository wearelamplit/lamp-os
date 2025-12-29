<script setup lang="ts">
import { ref } from 'vue'
import InfoPanel from '@/components/InfoPanel.vue'

interface Props {
  label?: string
  id?: string
  error?: string
  required?: boolean
  helpText?: string
  expandable?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  required: false,
  helpText: '',
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
  <div class="form-field">
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

    <InfoPanel v-else-if="helpText">
      {{ helpText }}
    </InfoPanel>
  </div>
</template>

<style scoped>
.form-field {
  display: flex;
  flex-direction: column;
  gap: 10px;
  width: 100%;
  margin-bottom: 32px;
  margin-top: 16px;
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
  font-size: 0.875rem;
  font-weight: 600;
  margin-top: 4px;
  display: flex;
  align-items: center;
  gap: 6px;
}

.form-field-error::before {
  content: '⚠️';
  font-size: 0.8rem;
}

/* Mobile optimizations */
@media (max-width: 768px) {
  .form-field-label {
    font-size: 0.8rem;
  }

  .form-field-error {
    font-size: 0.8rem;
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
