<script setup lang="ts">
import { ref, computed, watch } from 'vue'

interface Props {
  label: string
  modelValue?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  modelValue: undefined,
})

const emit = defineEmits<{
  'update:modelValue': [value: boolean]
}>()

// Internal state for uncontrolled mode
const internalExpanded = ref(false)

// Check if component is in controlled mode (modelValue is explicitly passed)
const isControlled = computed(() => props.modelValue !== undefined)

// Use external state when controlled, internal state when uncontrolled
const isExpanded = computed({
  get: () => isControlled.value ? props.modelValue : internalExpanded.value,
  set: (value) => {
    if (isControlled.value) {
      emit('update:modelValue', value)
    } else {
      internalExpanded.value = value
    }
  },
})

// Sync internal state with modelValue when it changes (for controlled mode)
watch(
  () => props.modelValue,
  (newValue) => {
    if (newValue !== undefined) {
      internalExpanded.value = newValue
    }
  },
)

const toggle = () => {
  isExpanded.value = !isExpanded.value
}

// Expose methods for external control
const open = () => {
  isExpanded.value = true
}

const close = () => {
  isExpanded.value = false
}

defineExpose({ open, close, toggle })
</script>

<template>
  <div class="collapsible-panel" :class="{ 'collapsible-panel--expanded': isExpanded }">
    <button type="button" class="collapsible-panel-header" @click="toggle">
      <span class="collapsible-panel-arrow" :class="{ 'collapsible-panel-arrow--expanded': isExpanded }">
        ▶
      </span>
      <span v-if="$slots.left" class="collapsible-panel-left">
        <slot name="left" />
      </span>
      <span class="collapsible-panel-label">{{ label }}</span>
    </button>
    <div v-show="isExpanded" class="collapsible-panel-content">
      <slot />
    </div>
  </div>
</template>

<style scoped>
.collapsible-panel {
  border: 1px solid var(--color-border);
  border-radius: 8px;
  overflow: hidden;
  background: var(--color-background-soft);
}

.collapsible-panel--expanded {
  border-color: var(--color-border-hover);
}

.collapsible-panel-header {
  display: flex;
  align-items: center;
  gap: 10px;
  width: 100%;
  padding: 12px 16px;
  background: transparent;
  border: none;
  cursor: pointer;
  color: var(--brand-lamp-white);
  font-size: 0.9rem;
  font-weight: 600;
  text-align: left;
  transition: background 0.2s ease;
}

.collapsible-panel-header:hover {
  background: var(--color-background-mute);
}

.collapsible-panel-arrow {
  font-size: 0.7rem;
  color: var(--brand-slate-grey);
  transition: transform 0.2s ease;
  flex-shrink: 0;
}

.collapsible-panel-arrow--expanded {
  transform: rotate(90deg);
}

.collapsible-panel-left {
  display: flex;
  align-items: center;
  flex-shrink: 0;
}

.collapsible-panel-label {
  flex: 1;
}

.collapsible-panel-content {
  padding: 0 16px 16px 16px;
  animation: slideDown 0.2s ease;
}

@keyframes slideDown {
  from {
    opacity: 0;
    transform: translateY(-8px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

/* Mobile optimizations */
@media (max-width: 480px) {
  .collapsible-panel-header {
    padding: 10px 12px;
    font-size: 0.85rem;
    gap: 8px;
  }

  .collapsible-panel-content {
    padding: 0 12px 12px 12px;
  }
}
</style>

