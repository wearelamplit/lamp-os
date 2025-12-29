<script setup lang="ts">
interface Props {
  hideIcon?: boolean
}

withDefaults(defineProps<Props>(), {
  hideIcon: false,
})
</script>

<template>
  <div class="info-panel">
    <span v-if="!hideIcon" class="info-panel-icon" aria-hidden="true">
      <svg width="14" height="14" viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
        <circle cx="8" cy="8" r="7" stroke="currentColor" stroke-width="1.5" fill="none" />
        <path d="M8 7V11" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" />
        <circle cx="8" cy="5" r="0.75" fill="currentColor" />
      </svg>
    </span>
    <div class="info-panel-content">
      <slot />
    </div>
  </div>
</template>

<style scoped>
.info-panel {
  margin-top: 8px;
  margin-left: 8px;
  margin-right: 8px;
  padding: 10px 14px 10px 12px;
  background: linear-gradient(135deg, rgba(68, 108, 156, 0.12) 0%, rgba(68, 108, 156, 0.06) 100%);
  border-left: 3px solid var(--brand-aurora-blue);
  border-radius: 2px 6px 6px 2px;
  font-size: 0.75rem;
  line-height: 1.5;
  color: var(--brand-fog-grey);
  display: flex;
  align-items: center;
  gap: 10px;
  letter-spacing: 0.01em;
}

.info-panel-icon {
  color: var(--brand-aurora-blue);
  flex-shrink: 0;
  display: flex;
  align-items: center;
  justify-content: center;
}

.info-panel-icon svg {
  width: 18px;
  height: 18px;
}

.info-panel-content {
  flex: 1;
}

.info-panel-content :deep(p) {
  margin: 0;
}

/* Mobile optimizations */
@media (max-width: 768px) {
  .info-panel {
    font-size: 0.75rem;
    padding: 8px 12px 8px 10px;
    gap: 8px;
  }

  .info-panel-icon svg {
    width: 16px;
    height: 16px;
  }
}
</style>

