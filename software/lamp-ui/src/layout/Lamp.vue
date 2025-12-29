<script setup lang="ts">
import { onMounted, onUnmounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import TopNavigation from '@/components/TopNavigation.vue'
import { useLampStore, tabs } from '@/stores/lamp'

const route = useRoute()
const router = useRouter()

const lampStore = useLampStore()

// Map route to tab ID
const getTabIdFromRoute = (path: string): string => {
  const tab = tabs.find((t) => t.path === path)
  return tab?.id || 'home'
}

// Handle tab change via navigation
const handleTabChange = (tabId: string) => {
  const tab = tabs.find((t) => t.id === tabId)
  if (tab) {
    router.push(tab.path)
  }
}

// Sync route changes to active tab
watch(
  () => route.path,
  (newPath) => {
    const tabId = getTabIdFromRoute(newPath)
    lampStore.setActiveTab(tabId)
  },
  { immediate: true },
)

onMounted(() => {
  lampStore.initialize()
})

onUnmounted(() => {
  lampStore.cleanup()
})
</script>

<template>
  <div class="lamp-layout">
    <!-- WebSocket Status Indicator -->
    <div
      class="ws-status-indicator"
      :class="{ connected: lampStore.wsConnected }"
      :title="lampStore.wsConnected ? 'WebSocket Connected' : 'WebSocket Disconnected'"
    >
      <div class="ws-status-dot"></div>
    </div>

    <div v-if="lampStore.loaded" class="container">
      <main class="main-content">
        <!-- Tab Navigation -->
        <TopNavigation :tabs="tabs" :active-tab="lampStore.activeTab" @update:active-tab="handleTabChange" />

        <!-- Page Content (Router View) -->
        <div class="tab-content">
          <router-view />
        </div>
      </main>
    </div>

    <!-- Loading State -->
    <div v-else class="loading-container">
      <div class="loading-spinner"></div>
      <p>Connecting to lamp...</p>
    </div>

    <!-- Floating Save Button -->
    <div v-if="lampStore.loaded" class="floating-save-container">
      <button
        class="floating-save-button"
        :class="{
          'has-changes': lampStore.hasChanges,
          saving: lampStore.saving,
          'no-changes': !lampStore.hasChanges || lampStore.disabled,
        }"
        @click="lampStore.saveSettings"
        :disabled="!lampStore.hasChanges || lampStore.saving || lampStore.disabled"
      >
        <span v-if="lampStore.disabled">Connecting...</span>
        <span v-else-if="lampStore.saving">Saving...</span>
        <span v-else-if="lampStore.hasChanges">Save Changes</span>
        <span v-else>No Changes</span>
      </button>
    </div>
  </div>
</template>

<style scoped>
.lamp-layout {
  min-height: 100vh;
  background: var(--brand-midnight-black);
  padding: 16px;
  padding-bottom: 10px !important;
  width: 100%;
}

.container {
  width: 100%;
  max-width: 100%;
  margin: 0 auto;
}

.main-content {
  background: var(--color-background-soft);
  border-radius: 16px;
  padding: 20px;
  padding-bottom: 40px !important;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
  backdrop-filter: blur(10px);
}

/* Loading State */
.loading-container {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 50vh;
  color: var(--brand-fog-grey);
}

.loading-spinner {
  width: 40px;
  height: 40px;
  border: 3px solid var(--color-border);
  border-top-color: var(--brand-aurora-blue);
  border-radius: 50%;
  animation: spin 1s linear infinite;
  margin-bottom: 16px;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

/* Tab Content Styles */
.tab-content {
  min-height: 200px;
}

/* Floating Save Button Styles */
.floating-save-container {
  position: fixed;
  bottom: 15px;
  z-index: 1000;
  pointer-events: none;
  display: flex;
  justify-content: center;
  width: 100%;
}

.floating-save-button {
  pointer-events: auto;
  padding: 16px 32px;
  border: none;
  border-radius: 50px;
  font-size: 1rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.3s ease;
  box-shadow:
    0 20px 60px rgba(0, 0, 0, 0.4),
    0 8px 32px rgba(0, 0, 0, 0.3),
    0 0 0 1px rgba(255, 255, 255, 0.1);
  backdrop-filter: blur(10px);
  font-family: inherit;
  min-width: 160px;
  text-align: center;
}

.floating-save-button.no-changes {
  background: var(--color-background-mute) !important;
  color: var(--brand-slate-grey);
  cursor: not-allowed;
}

.floating-save-button.has-changes {
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
  color: var(--brand-lamp-white);
  cursor: pointer;
}

.floating-save-button.has-changes:hover {
  transform: translateY(-4px);
  box-shadow:
    0 25px 80px rgba(0, 0, 0, 0.5),
    0 15px 50px rgba(68, 108, 156, 0.4),
    0 0 0 1px rgba(253, 253, 253, 0.2),
    0 0 30px rgba(68, 108, 156, 0.4);
}

.floating-save-button.saving {
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-lumen-green));
  color: var(--brand-lamp-white);
  cursor: not-allowed;
  opacity: 0.8;
}

.floating-save-button:disabled {
  cursor: not-allowed;
}

/* WebSocket Status Indicator */
.ws-status-indicator {
  position: fixed;
  bottom: 16px;
  right: 16px;
  z-index: 1001;
  background: rgba(0, 0, 0, 0.6);
  border-radius: 50%;
  backdrop-filter: blur(10px);
  transition: all 0.3s ease;
}

.ws-status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--color-error);
  transition: all 0.3s ease;
  box-shadow: 0 0 8px rgba(248, 113, 113, 0.5);
}

.ws-status-indicator.connected .ws-status-dot {
  background: var(--color-success);
  box-shadow: 0 0 8px rgba(141, 205, 166, 0.5);
}

/* Mobile-first design */
@media (min-width: 480px) {
  .container {
    max-width: 400px;
  }

  .lamp-layout {
    padding: 20px;
    padding-bottom: 40px !important;
  }

  .main-content {
    padding: 20px;
    padding-bottom: 40px !important;
  }
}

@media (min-width: 1024px) {
  .container {
    max-width: 450px;
  }
}

/* Mobile adjustments */
@media (max-width: 479px) {
  .floating-save-container {
    bottom: 16px;
    left: 16px;
    right: 16px;
    transform: none;
    padding: 0 16px;
  }

  .floating-save-button {
    width: 100%;
    max-width: 300px;
    min-width: auto;
  }

  .ws-status-indicator {
    bottom: 12px;
    right: 12px;
  }
}
</style>
