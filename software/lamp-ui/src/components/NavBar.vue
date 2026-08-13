<script setup lang="ts">
interface Tab {
  id: string
  label: string
}

defineProps<{ tabs: Tab[]; activeTab: string }>()
defineEmits<{ (e: 'update:activeTab', value: string): void }>()

const icons: Record<string, string> = {
  home: '<path d="M3 10.5 12 3l9 7.5"/><path d="M5 9.5V21h14V9.5"/>',
  expressions: '<path d="M12 2.5 14.4 9 21 11.4 14.4 13.8 12 20.4 9.6 13.8 3 11.4 9.6 9z"/>',
  social:
    '<circle cx="9" cy="8" r="3"/><path d="M3.5 20c0-3 2.5-5 5.5-5s5.5 2 5.5 5"/><path d="M16 6a3 3 0 0 1 0 6"/><path d="M17 15.2c2.3.5 3.5 2.2 3.5 4.8"/>',
  'lamp-setup':
    '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.6a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>',
  info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v5"/><path d="M12 7.6h.01"/>',
}

const iconSvg = (id: string) =>
  `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">${icons[id] ?? ''}</svg>`
</script>

<template>
  <nav class="tab-navigation">
    <button
      v-for="tab in tabs"
      :key="tab.id"
      type="button"
      class="tab-button"
      :class="{ active: activeTab === tab.id }"
      @click="$emit('update:activeTab', tab.id)"
    >
      <span class="tab-icon" v-html="iconSvg(tab.id)"></span>
      <span class="tab-label">{{ tab.label }}</span>
    </button>
  </nav>
</template>

<style scoped>
.tab-navigation {
  position: fixed;
  left: 0;
  right: 0;
  bottom: 0;
  z-index: 999;
  display: flex;
  width: 100%;
  background: var(--color-background-soft);
  border-top: 1px solid var(--color-border);
  backdrop-filter: blur(10px);
  padding-bottom: env(safe-area-inset-bottom);
}

.tab-button {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  padding: 10px 2px;
  background: transparent;
  border: none;
  color: var(--brand-slate-grey);
  font-family: inherit;
  font-size: 0.72rem;
  font-weight: 600;
  cursor: pointer;
  transition:
    color 0.2s ease,
    background 0.2s ease;
}

.tab-button:hover {
  color: var(--brand-fog-grey);
}

.tab-button.active {
  color: var(--brand-lamp-white);
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
}

.tab-icon {
  display: block;
  width: 22px;
  height: 22px;
}

.tab-icon :deep(svg) {
  display: block;
  width: 100%;
  height: 100%;
}

.tab-label {
  line-height: 1;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 100%;
}

@media (max-width: 360px) {
  .tab-button {
    font-size: 0.65rem;
    padding: 9px 1px;
  }

  .tab-icon {
    width: 20px;
    height: 20px;
  }
}
</style>
