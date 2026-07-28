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
            @open="emit('edit-session', true)"
            @close="emit('edit-session', false)"
            :disabled="disabled"
          />
        </div>

        <div class="color-actions">
          <IconButton
            v-if="localColors.length < props.maxColors"
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

    <div v-if="props.showAddButton" class="add-button-container">
      <IconButton
        icon="plus"
        variant="plus"
        title="Add color"
        :disabled="localColors.length >= props.maxColors || disabled"
        @click="addColor"
      />
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, computed } from 'vue'
import ColorPicker from './ColorPicker.vue'
import IconButton from './IconButton.vue'
import { createGradientFromHexww } from '../utils/colorUtils'

interface Props {
  modelValue: string[]
  showAddButton?: boolean
  maxColors?: number
  disabled?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  modelValue: () => ['#FF000000'],
  showAddButton: true,
  maxColors: 5,
  disabled: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: string[]]
  'edit-session': [open: boolean]
}>()

const localColors = ref<string[]>([...props.modelValue])

// Ensure we always have at least one color
if (localColors.value.length === 0) {
  localColors.value = ['#FF000000']
}

const gradientStyle = computed(() => {
  if (localColors.value.length <= 1) return {}

  return {
    background: createGradientFromHexww(localColors.value, 'to right'),
  }
})

const updateColor = (index: number, value: string) => {
  localColors.value[index] = value
  emit('update:modelValue', [...localColors.value])
}

const addColor = () => {
  const firstColor = localColors.value[0] || '#FF000000'
  localColors.value.unshift(firstColor)
  emit('update:modelValue', [...localColors.value])
}

const removeColor = (index: number) => {
  if (localColors.value.length > 1) {
    localColors.value.splice(index, 1)
    emit('update:modelValue', [...localColors.value])
  }
}

const cloneColor = (index: number) => {
  if (localColors.value.length < props.maxColors) {
    const colorToClone = localColors.value[index]
    localColors.value.splice(index, 0, colorToClone)
    emit('update:modelValue', [...localColors.value])
  }
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

/* Mobile optimizations */
@media (max-width: 480px) {
  .color-list {
    gap: 10px;
  }
}
</style>
