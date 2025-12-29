<template>
  <div
    class="color-preview"
    :class="[`color-preview-${size}`, { disabled }]"
    @click="$emit('click', $event)"
  >
    <!-- Base RGB color layer -->
    <div class="color-layer base-layer" :style="{ backgroundColor: baseColor }"></div>

    <!-- Warm white overlay layer -->
    <div
      v-if="warmWhiteOpacity > 0"
      class="color-layer warm-white-layer"
      :style="{
        backgroundColor: warmWhiteColor,
        opacity: warmWhiteOpacity,
      }"
    ></div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { parseHexww } from '@/lib/colorUtils'

interface Props {
  hexValue: string
  size?: 'small' | 'large'
  disabled?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  size: 'small',
  disabled: false,
})

// Parse hexww value and extract RGB and WW components
const colorComponents = computed(() => parseHexww(props.hexValue))

// Base RGB color for the background layer
const baseColor = computed(() => {
  const { red, green, blue } = colorComponents.value
  return `rgb(${red}, ${green}, ${blue})`
})

// Warm white color (warm orange/yellow tone)
const warmWhiteColor = computed(() => {
  return '#fabb3e'
})

// Calculate RGB sum (0-765)
const rgbSum = computed(() => {
  const { red, green, blue } = colorComponents.value
  return red + green + blue
})

// Calculate available room (0-765)
const availableRoom = computed(() => {
  return 765 - rgbSum.value
})

// Calculate warm white opacity based on available room and WW value
const warmWhiteOpacity = computed(() => {
  const { warmWhite } = colorComponents.value
  const room = availableRoom.value

  // If no room available, no WW overlay
  if (room <= 0) return 0

  // Calculate room percentage (0-1)
  const roomPercentage = room / 765

  // Calculate WW percentage (0-1)
  const wwPercentage = warmWhite / 255

  // Final opacity is WW percentage of available room percentage
  return wwPercentage * roomPercentage
})
</script>

<style scoped>
.color-preview {
  position: relative;
  border: 0;
  cursor: pointer;
  transition: transform 0.2s ease;
  overflow: hidden;
  border-radius: 2px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.3);
}

.color-preview:hover {
  transform: scale(1.05);
}

.color-preview.disabled {
  opacity: 0.5;
  cursor: not-allowed;
  pointer-events: none;
}

.color-preview.disabled:hover {
  transform: none;
}

.color-layer {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
}

.base-layer {
  z-index: 1;
}

.warm-white-layer {
  z-index: 2;
  mix-blend-mode: screen;
}

.color-preview-small {
  width: 120px;
  max-width: 100%;
  height: 40px;
}

.color-preview-large {
  width: 100%;
  height: 80px;
}

/* Mobile-first responsive design */
@media (max-width: 480px) {
  .color-preview-small {
    width: 35px;
    height: 35px;
  }
}
</style>
