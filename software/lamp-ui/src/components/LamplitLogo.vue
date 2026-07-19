<script setup lang="ts">
// Bound src (not a static attribute) so Vite treats it as a runtime URL, not a
// bundled asset. The firmware serves this from SPIFFS; color is baked into the
// SVG file (fill #fdfdfd), so it ships once instead of inlined here too.
const logoUrl = '/logo.svg'
</script>

<template>
  <img class="logo" :src="logoUrl" alt="Lamplit" />
</template>
