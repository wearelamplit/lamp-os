<template>
  <div class="color-gradient">
    <div v-if="isGradient && localColors.length > 1" class="gradient-preview-container">
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
            v-if="hasActiveColor && localColors.length > 1"
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
            v-if="localColors.length < maxColorsComputed"
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
        :disabled="localColors.length >= maxColorsComputed || disabled"
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
  activeColor?: number  // Current active color index (from parent/store)
  hasActiveColor?: boolean  // Whether to show the star button
  isGradient?: boolean  // Whether to show the gradient preview
  required?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  modelValue: () => ['#FF0000FF'],
  showAddButton: true,
  maxColors: 5,
  max: undefined,
  disabled: false,
  activeColor: 0,
  hasActiveColor: false,
  isGradient: false,
  required: false,
})

// Use max as alias for maxColors if provided
const maxColorsComputed = computed(() => props.max ?? props.maxColors)

const emit = defineEmits<{
  'update:modelValue': [value: string[]]
  'meta': [value: { activeColor: number }]
}>()

const localColors = ref<string[]>([...props.modelValue])
const localActiveColor = ref<number>(props.activeColor)

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
  return localActiveColor.value === index
}

/**
 * Emit active color meta event
 */
const emitActiveColorMeta = (index: number) => {
  localActiveColor.value = index
  emit('meta', { activeColor: index })
}

/**
 * Ensure active color is within valid range
 * Returns the corrected index
 */
const ensureValidActiveColor = (colorsLength: number, currentActive: number): number => {
  if (colorsLength === 0) return 0
  if (colorsLength === 1) return 0
  if (currentActive >= colorsLength) return colorsLength - 1
  if (currentActive < 0) return 0
  return currentActive
}

const updateColor = (index: number, value: string) => {
  localColors.value[index] = value
  emit('update:modelValue', [...localColors.value])
}

const addColor = () => {
  const firstColor = localColors.value[0] || '#FF0000FF'
  localColors.value.unshift(firstColor)

  // If hasActiveColor and active color exists, increment index since we added at beginning
  if (props.hasActiveColor && localActiveColor.value >= 0) {
    const newActive = localActiveColor.value + 1
    emitActiveColorMeta(newActive)
  }

  emit('update:modelValue', [...localColors.value])
}

const removeColor = (index: number) => {
  if (localColors.value.length > 1) {
    localColors.value.splice(index, 1)

    // Handle active color when removing
    if (props.hasActiveColor) {
      let newActive = localActiveColor.value

      if (localActiveColor.value === index) {
        // Removed the active color - set to the one above (or 0 if at start)
        newActive = index > 0 ? index - 1 : 0
      } else if (localActiveColor.value > index) {
        // Active color was after removed one - decrement index
        newActive = localActiveColor.value - 1
      }

      // Ensure valid range
      newActive = ensureValidActiveColor(localColors.value.length, newActive)
      emitActiveColorMeta(newActive)
    }

    emit('update:modelValue', [...localColors.value])
  }
}

const setActiveColor = (index: number) => {
  emitActiveColorMeta(index)
}

const cloneColor = (index: number) => {
  if (localColors.value.length < maxColorsComputed.value) {
    const colorToClone = localColors.value[index]
    localColors.value.splice(index, 0, colorToClone)

    // If hasActiveColor and active color is at or after cloned position, increment
    if (props.hasActiveColor && localActiveColor.value >= index) {
      const newActive = localActiveColor.value + 1
      emitActiveColorMeta(newActive)
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

// Watch for external changes to colors
watch(
  () => props.modelValue,
  (newValue) => {
    if (newValue && newValue.length > 0) {
      localColors.value = [...newValue]

      // Ensure active color is still valid after external color update
      if (props.hasActiveColor) {
        const validActive = ensureValidActiveColor(localColors.value.length, localActiveColor.value)
        if (validActive !== localActiveColor.value) {
          emitActiveColorMeta(validActive)
        }
      }
    }
  },
  { deep: true },
)

// Watch for external activeColor prop changes
watch(
  () => props.activeColor,
  (newValue) => {
    if (newValue !== undefined && newValue !== localActiveColor.value) {
      localActiveColor.value = ensureValidActiveColor(localColors.value.length, newValue)
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
