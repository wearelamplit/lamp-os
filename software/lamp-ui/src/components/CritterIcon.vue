<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { critterIndexFor } from '../utils/critterIndex'
import { critterAssetPath, fetchCritterTemplate, recolorCritterSvg } from '../utils/critterAsset'
import { hexwwToRgb } from '../utils/colorUtils'

const props = withDefaults(
  defineProps<{ lampId?: string; shade: string; base: string; stray?: boolean }>(),
  { stray: false },
)

const assetPath = computed(() =>
  props.lampId ? critterAssetPath(props.stray ? 'stray' : critterIndexFor(props.lampId)) : '',
)

const template = ref('')

watch(
  assetPath,
  async (path) => {
    template.value = ''
    if (!path) return
    try {
      const text = await fetchCritterTemplate(path)
      if (assetPath.value === path) template.value = text
    } catch {
      // A 404 or network failure leaves the icon empty.
    }
  },
  { immediate: true },
)

const svgMarkup = computed(() =>
  template.value ? recolorCritterSvg(template.value, hexwwToRgb(props.shade), hexwwToRgb(props.base)) : '',
)
</script>

<template>
  <span
    v-if="svgMarkup"
    class="critter-icon"
    role="img"
    aria-label="Your lamp's critter"
    v-html="svgMarkup"
  />
</template>

<style scoped>
.critter-icon {
  display: block;
  width: 100%;
}

.critter-icon :deep(svg) {
  display: block;
  width: 100%;
  height: auto;
}
</style>
