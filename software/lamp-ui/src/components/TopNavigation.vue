<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from 'vue'

interface Tab {
  id: string
  label: string
}

interface Props {
  tabs: Tab[]
  activeTab: string
}

interface Emits {
  (e: 'update:activeTab', value: string): void
}

const props = defineProps<Props>()
const emit = defineEmits<Emits>()

// Get current page title
const currentPageTitle = computed(() => {
  const activeTab = props.tabs.find((tab) => tab.id === props.activeTab)
  return activeTab?.label || ''
})

// Mobile menu state
const isMobileMenuOpen = ref(false)
const isMobile = ref(false)

// Check if screen is mobile size
const checkMobile = () => {
  isMobile.value = window.innerWidth < 480
  if (!isMobile.value) {
    isMobileMenuOpen.value = false
  }
}

// Toggle mobile menu
const toggleMobileMenu = () => {
  isMobileMenuOpen.value = !isMobileMenuOpen.value
}

// Close mobile menu when clicking outside
const closeMobileMenu = () => {
  isMobileMenuOpen.value = false
}

// Handle mobile tab click
const handleMobileTabClick = (tabId: string) => {
  emit('update:activeTab', tabId)
  closeMobileMenu()
}

onMounted(() => {
  checkMobile()
  window.addEventListener('resize', checkMobile)
  document.addEventListener('click', (e) => {
    const target = e.target as HTMLElement
    if (!target.closest('.tab-navigation')) {
      closeMobileMenu()
    }
  })
})

onUnmounted(() => {
  window.removeEventListener('resize', checkMobile)
})
</script>

<template>
  <div class="tab-navigation">
    <!-- Mobile page title (only shown on mobile) -->
    <div v-if="isMobile" class="mobile-page-title">
      {{ currentPageTitle }}
    </div>

    <!-- Desktop navigation -->
    <div v-if="!isMobile" class="desktop-nav">
      <button
        v-for="tab in tabs"
        :key="tab.id"
        class="tab-button"
        :class="{ active: activeTab === tab.id }"
        @click="$emit('update:activeTab', tab.id)"
      >
        {{ tab.label }}
      </button>
    </div>

    <!-- Mobile navigation -->
    <div v-else class="mobile-nav">
      <!-- Mobile menu button -->
      <button
        class="mobile-menu-button"
        :class="{ active: isMobileMenuOpen }"
        @click="toggleMobileMenu"
        aria-label="Toggle navigation menu"
      >
        <span class="hamburger-line"></span>
        <span class="hamburger-line"></span>
        <span class="hamburger-line"></span>
      </button>

      <!-- Mobile dropdown menu -->
      <div v-if="isMobileMenuOpen" class="mobile-dropdown" @click.stop>
        <button
          v-for="tab in tabs"
          :key="tab.id"
          class="mobile-tab-button"
          :class="{ active: activeTab === tab.id }"
          @click="handleMobileTabClick(tab.id)"
        >
          {{ tab.label }}
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* Tab Navigation Styles */
.tab-navigation {
  display: flex;
  align-items: center;
  background: var(--color-background-mute);
  border-radius: 12px;
  padding: 4px;
  margin-bottom: 24px;
  border: 0px solid var(--color-border);
  position: relative;
}

/* Compact mobile navigation */
.tab-navigation:has(.mobile-nav) {
  padding: 2px;
  margin-bottom: 16px;
}

/* Desktop Navigation */
.desktop-nav {
  display: flex;
  width: 100%;
}

.tab-button {
  flex: 1;
  padding: 12px 16px;
  background: transparent;
  border: none;
  color: var(--brand-slate-grey);
  font-size: 0.9rem;
  font-weight: 500;
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.2s ease;
  font-family: inherit;
  white-space: nowrap;
}

.tab-button:hover {
  color: var(--brand-fog-grey);
  background: rgba(253, 253, 253, 0.05);
}

.tab-button.active {
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
  color: var(--brand-lamp-white);
  box-shadow: 0 2px 8px rgba(68, 108, 156, 0.3);
}

/* Mobile Navigation */
.mobile-nav {
  display: flex;
  width: auto;
  justify-content: flex-end;
  align-items: center;
  position: relative;
  margin-left: auto;
}

.mobile-page-title {
  color: var(--brand-slate-grey);
  font-size: 1rem;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 200px;
  margin-right: auto;
  margin-left: 12px;
}

.mobile-menu-button {
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  width: 44px;
  height: 44px;
  background: transparent;
  border: none;
  cursor: pointer;
  border-radius: 8px;
  transition: all 0.2s ease;
  padding: 6px;
}

.mobile-menu-button:hover {
  background: rgba(253, 253, 253, 0.05);
}

.mobile-menu-button.active {
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
  box-shadow: 0 2px 8px rgba(68, 108, 156, 0.3);
}

.hamburger-line {
  width: 20px;
  height: 2px;
  background: var(--brand-slate-grey);
  margin: 2px 0;
  transition: all 0.2s ease;
  border-radius: 1px;
}

.mobile-menu-button.active .hamburger-line {
  background: var(--brand-lamp-white);
}

.mobile-menu-button.active .hamburger-line:nth-child(1) {
  transform: rotate(45deg) translate(5px, 5px);
}

.mobile-menu-button.active .hamburger-line:nth-child(2) {
  opacity: 0;
}

.mobile-menu-button.active .hamburger-line:nth-child(3) {
  transform: rotate(-45deg) translate(7px, -6px);
}

/* Mobile Dropdown */
.mobile-dropdown {
  position: absolute;
  top: 100%;
  right: 0;
  background: var(--color-background-mute);
  border-radius: 12px;
  padding: 8px;
  margin-top: 4px;
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.1);
  border: 1px solid var(--color-border);
  z-index: 1000;
  min-width: 200px;
  animation: slideDown 0.2s ease-out;
}

@keyframes slideDown {
  from {
    opacity: 0;
    transform: translateY(-10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.mobile-tab-button {
  display: block;
  width: 100%;
  padding: 12px 16px;
  background: transparent;
  border: none;
  color: var(--brand-slate-grey);
  font-size: 0.9rem;
  font-weight: 500;
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.2s ease;
  font-family: inherit;
  text-align: left;
  margin-bottom: 4px;
}

.mobile-tab-button:last-child {
  margin-bottom: 0;
}

.mobile-tab-button:hover {
  color: var(--brand-fog-grey);
  background: rgba(253, 253, 253, 0.05);
}

.mobile-tab-button.active {
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
  color: var(--brand-lamp-white);
  box-shadow: 0 2px 8px rgba(68, 108, 156, 0.3);
}

/* Responsive breakpoint */
@media (max-width: 479px) {
  .desktop-nav {
    display: none;
  }
}

@media (min-width: 480px) {
  .mobile-nav {
    display: none;
  }
}
</style>
