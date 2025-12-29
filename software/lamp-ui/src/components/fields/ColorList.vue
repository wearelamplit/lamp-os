<template>
  <div class="color-gradient">
    <div v-if="localColors.length > 1" class="gradient-preview-container">
      <div class="gradient-preview" :style="gradientStyle"></div>
      <span class="indicator-right">Top</span>
    </div>
    <div class="color-list">
      <div
        v-for="(color, index) in localColors.slice().reverse()"
        :key="localColors.length - 1 - index"
        class="color-item"
      >
        <div class="color-swatch-group">
          <ColorPicker
            v-model="localColors[localColors.length - 1 - index]"
            @update:model-value="updateColor(localColors.length - 1 - index, $event)"
            :disabled="disabled"
          />

          <IconButton
            v-if="localColors.length > 1"
            icon="star"
            variant="star"
            :title="
              isActiveColor(localColors.length - 1 - index) ? 'Active color' : 'Set as active'
            "
            :disabled="disabled"
            :class="{ active: isActiveColor(localColors.length - 1 - index) }"
            @click="setActiveColor(localColors.length - 1 - index)"
          />
        </div>

        <div class="color-actions">
          <IconButton
            v-if="localColors.length < maxColors"
            icon="clone"
            variant="clone"
            title="Clone color"
            :disabled="disabled"
            @click="cloneColor(localColors.length - 1 - index)"
          />

          <IconButton
            v-if="localColors.length > 1"
            icon="remove"
            variant="remove"
            title="Remove color"
            :disabled="disabled"
            @click="removeColor(localColors.length - 1 - index)"
          />
        </div>
      </div>
    </div>

    <div v-if="showAddButton" class="add-button-container">
      <IconButton
        icon="plus"
        variant="plus"
        title="Add color"
        :disabled="localColors.length >= maxColors || disabled"
        @click="addColor"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, computed } from 'vue'
import ColorPicker from './Color.vue'
import IconButton from './IconButton.vue'
import { createGradientFromHexww } from '@/lib/colorUtils'
import type { FieldValidationResult } from '@/types'

interface Props {
  modelValue: string[]
  showAddButton?: boolean
  maxColors?: number
  max?: number  // Alias for maxColors for form compatibility
  disabled?: boolean
  activeColor?: number
  required?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  modelValue: () => ['#FF0000FF'],
  showAddButton: true,
  maxColors: 5,
  max: undefined,
  disabled: false,
  activeColor: 0,
  required: false,
})

// Use max as alias for maxColors if provided
const maxColors = computed(() => props.max ?? props.maxColors)

const emit = defineEmits<{
  'update:modelValue': [value: string[]]
  'update:activeColor': [value: number]
}>()

const localColors = ref<string[]>([...props.modelValue])

// Ensure we always have at least one color
if (localColors.value.length === 0) {
  localColors.value = ['#FF0000FF']
}

const gradientStyle = computed(() => {
  if (localColors.value.length <= 1) return {}

  return {
    background: createGradientFromHexww(localColors.value, 'to right'),
  }
})

const isActiveColor = (index: number) => {
  return props.activeColor === index
}

const updateColor = (index: number, value: string) => {
  localColors.value[index] = value
  emit('update:modelValue', [...localColors.value])
}

const addColor = () => {
  const firstColor = localColors.value[0] || '#FF0000FF'
  localColors.value.unshift(firstColor)

  // If active color exists and is not at index 0, increment its index
  if (props.activeColor !== undefined && props.activeColor >= 0) {
    emit('update:activeColor', props.activeColor + 1)
  }

  emit('update:modelValue', [...localColors.value])
}

const removeColor = (index: number) => {
  if (localColors.value.length > 1) {
    localColors.value.splice(index, 1)

    // If active color exists and is at or after the removed position, decrement its index
    if (props.activeColor !== undefined && props.activeColor >= index) {
      // If we're removing the active color itself, set it to 0 (first color)
      // Otherwise, decrement the index
      const newActiveIndex = props.activeColor === index ? 0 : props.activeColor - 1
      emit('update:activeColor', newActiveIndex)
    }

    emit('update:modelValue', [...localColors.value])
  }
}

const setActiveColor = (index: number) => {
  emit('update:activeColor', index)
}

const cloneColor = (index: number) => {
  if (localColors.value.length < maxColors.value) {
    const colorToClone = localColors.value[index]
    localColors.value.splice(index, 0, colorToClone)

    // If active color exists and is at or after the cloned position, increment its index
    if (props.activeColor !== undefined && props.activeColor >= index) {
      emit('update:activeColor', props.activeColor + 1)
    }

    emit('update:modelValue', [...localColors.value])
  }
}

// Validation method exposed to form
const validate = (): FieldValidationResult => {
  if (props.required && localColors.value.length === 0) {
    return { valid: false, error: 'At least one color is required' }
  }
  return { valid: true }
}

// Watch for external changes
watch(
  () => props.modelValue,
  (newValue) => {
    if (newValue && newValue.length > 0) {
      localColors.value = [...newValue]
    }
  },
  { deep: true },
)

// Watch for activeColor changes to ensure UI updates
watch(
  () => props.activeColor,
  (newValue) => {
    // Ensure activeColor is within valid range
    if (newValue !== undefined && (newValue < 0 || newValue >= localColors.value.length)) {
      console.warn(`activeColor ${newValue} is out of range for ${localColors.value.length} colors`)
    }
  },
)

defineExpose({ validate })
</script>

<style scoped>
.color-gradient {
  width: 100%;
  position: relative;
}

.gradient-preview-container {
  position: relative;
  margin-bottom: 12px;
  display: flex;
  align-items: center;
  gap: 8px;
}

.gradient-preview {
  flex: 1;
  height: 20px;
  border-radius: 2px;
  pointer-events: none;
}

.indicator-right {
  font-size: 10px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  writing-mode: vertical-rl;
  text-orientation: mixed;
  color: #efa3c8;
}

.color-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-bottom: 12px;
}

.color-item {
  display: flex;
  align-items: center;
  gap: 12px;
  justify-content: space-between;
}

.color-swatch-group {
  display: flex;
  align-items: center;
  gap: 4px;
  flex: 1;
}

.color-actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.add-button-container {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-top: 8px;
}

/* Active star button styling */
:deep(.icon-button.active) {
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
  color: var(--brand-lamp-white);
  box-shadow: 0 2px 8px rgba(68, 108, 156, 0.3);
}

:deep(.icon-button.active:hover) {
  background: linear-gradient(135deg, var(--color-accent-hover), var(--brand-glow-pink));
  box-shadow: 0 4px 12px rgba(68, 108, 156, 0.4);
}

/* Mobile optimizations */
@media (max-width: 480px) {
  .color-list {
    gap: 10px;
  }
}
</style>
