<script setup lang="ts">
import { computed } from 'vue'

interface Props {
  label: string
  headingIndex?: number
}

const props = withDefaults(defineProps<Props>(), {
  headingIndex: 0,
})

// Color rotation array using CSS variable names
const headingColors = [
  'var(--brand-amber-gold)',
  'var(--header-yellow)',
  'var(--header-lime)',
  'var(--brand-lumen-green)',
  'var(--brand-glow-pink)',
]

// Calculate the color based on the heading index
const headingColor = computed(() => {
  return headingColors[props.headingIndex % headingColors.length]
})
</script>

<template>
  <h1 class="group-heading" :style="{ color: headingColor }">
    {{ label }}
  </h1>
</template>

<style scoped>
.group-heading {
  font-size: 1rem;
  font-weight: 800;
  margin-top: 8px;
  margin-bottom: 0;
}
</style>

