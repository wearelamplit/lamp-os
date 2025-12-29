<script setup lang="ts"></script>

<template>
  <div class="info-panel">
    <slot />
  </div>
</template>

<style scoped>
.info-panel {
  margin: 8px;
  padding: 10px 14px;
  background: linear-gradient(135deg, rgba(68, 108, 156, 0.12) 0%, rgba(68, 108, 156, 0.06) 100%);
  border-left: 3px solid var(--brand-aurora-blue);
  border-radius: 2px 6px 6px 2px;
  font-size: 0.75rem;
  line-height: 1.5;
  color: var(--brand-fog-grey);
  letter-spacing: 0.01em;
}

.info-panel :deep(p) {
  margin: 0;
}

/* Mobile optimizations */
@media (max-width: 768px) {
  .info-panel {
    font-size: 0.75rem;
    padding: 8px 12px;
  }
}
</style>
