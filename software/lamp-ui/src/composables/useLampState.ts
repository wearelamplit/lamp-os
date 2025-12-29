/**
 * Lamp State Composable
 *
 * Provides shared state management for lamp settings, WebSocket connection,
 * and related functionality across all lamp pages.
 */

import { ref, computed, watch, onMounted, onUnmounted, type Ref } from 'vue'
import type { Settings } from '@/types'

// Additional type definitions for expressions
export interface Expression {
  type: string
  enabled: boolean
  colors: string[]
  intervalMin: number
  intervalMax: number
  target: number
  duration?: number
  durationMin?: number
  durationMax?: number
}

// Extend Settings interface
declare module '@/types' {
  interface Settings {
    expressions?: Expression[]
  }
}

// Configuration constants
const MAX_RECONNECT_ATTEMPTS = 60
const RECONNECT_INTERVAL = 2500
const WEBSOCKET_DEBOUNCE_INTERVAL = 10
export const MAX_LEDS_BASE = 50

// Shared reactive state (singleton pattern)
const settings = ref<Settings>({})
const loaded = ref(false)
const disabled = ref(false)
const originalSettings = ref<string>('')
const saving = ref(false)
const resetUnsavedChanges = ref(0)
const activeTab = ref('home')

const ws = ref<WebSocket | null>(null)
const wsConnected = ref(false)
const reconnectAttempts = ref(0)
let reconnectTimeout: number | null = null
let websocketDebounceTimeout: number | null = null

// Track if already initialized
let isInitialized = false

// Tab configuration
export const tabs = [
  { id: 'home', label: 'Home', path: '/' },
  { id: 'expressions', label: 'Expressions', path: '/expressions' },
  { id: 'lamp-setup', label: 'Setup', path: '/setup' },
  { id: 'info', label: 'Info', path: '/info' },
]

export function useLampState() {
  // Computed property to check if settings have changed
  const hasChanges = computed(() => {
    return JSON.stringify(settings.value) !== originalSettings.value
  })

  // WebSocket send with debouncing
  const websocketSend = (action: Record<string, unknown>) => {
    if (websocketDebounceTimeout) {
      clearTimeout(websocketDebounceTimeout)
    }

    websocketDebounceTimeout = window.setTimeout(() => {
      ws.value?.send(JSON.stringify(action))
      websocketDebounceTimeout = null
    }, WEBSOCKET_DEBOUNCE_INTERVAL)
  }

  // Connect to WebSocket
  const connectWebSocket = () => {
    if (reconnectTimeout) {
      clearTimeout(reconnectTimeout)
      reconnectTimeout = null
    }

    if (ws.value) {
      ws.value.close()
    }

    ws.value = new WebSocket(`${import.meta.env.VITE_SERVER_WS}`)

    ws.value.onopen = () => {
      wsConnected.value = true
      disabled.value = false
      reconnectAttempts.value = 0

      // Send current brightness based on home mode state
      if (settings.value.lamp) {
        const brightness = settings.value.lamp.homeMode
          ? (settings.value.lamp.homeModeBrightness ?? 80)
          : (settings.value.lamp.brightness ?? 100)
        websocketSend({ a: 'bright', v: brightness })
      }

      // Send current tab state
      websocketSend({ a: 'tab', v: activeTab.value })

      // Send current colors to establish preview
      if (settings.value.shade?.colors) {
        websocketSend({ a: 'shade', c: settings.value.shade.colors })
      }
      if (settings.value.base?.colors) {
        websocketSend({ a: 'base', c: settings.value.base.colors })
      }

      ws.value?.send(
        JSON.stringify({
          type: 'test',
          message: 'Hello WebSocket!',
          timestamp: new Date().toISOString(),
        }),
      )
    }

    ws.value.onclose = () => {
      wsConnected.value = false
      disabled.value = true
      if (reconnectAttempts.value < MAX_RECONNECT_ATTEMPTS) {
        reconnectAttempts.value++
        reconnectTimeout = window.setTimeout(() => {
          connectWebSocket()
        }, RECONNECT_INTERVAL)
      } else {
        console.log('Max reconnection attempts reached. Stopping reconnection attempts.')
      }
    }

    ws.value.onerror = (error) => {
      console.error('WebSocket error:', error)
      wsConnected.value = false
      disabled.value = true
    }

    return ws.value
  }

  // Generic function to update settings
  const updateSetting = (path: string, value: unknown) => {
    const pathParts = path.split('.')
    let current: Record<string, unknown> = settings.value

    for (let i = 0; i < pathParts.length - 1; i++) {
      if (!current[pathParts[i]]) {
        current[pathParts[i]] = {}
      }
      current = current[pathParts[i]] as Record<string, unknown>
    }

    const finalKey = pathParts[pathParts.length - 1]
    current[finalKey] = value

    let action: Record<string, unknown> | undefined
    switch (path) {
      case 'lamp.brightness':
        if (!settings.value.lamp?.homeMode) {
          action = { a: 'bright', v: value }
        }
        break
      case 'lamp.homeModeBrightness':
        if (settings.value.lamp?.homeMode) {
          action = { a: 'bright', v: value }
        }
        break
      case 'lamp.homeMode':
        if (value) {
          action = { a: 'bright', v: settings.value.lamp?.homeModeBrightness ?? 80 }
        } else {
          action = { a: 'bright', v: settings.value.lamp?.brightness || 100 }
        }
        break
      case 'shade.colors':
        action = { a: 'shade', c: value }
        break
      case 'base.colors':
        action = { a: 'base', c: value }
        break
    }
    if (action) {
      websocketSend(action)
    }
  }

  // Update knockout pixel brightness
  const updateKnockoutPixel = (ledIndex: number, brightness: number) => {
    if (!settings.value.base) {
      settings.value.base = {}
    }
    if (!settings.value.base?.knockout) {
      settings.value.base.knockout = []
    }

    const existingIndex = settings.value.base.knockout.findIndex((kp) => kp.p === ledIndex)

    if (brightness === 100) {
      if (existingIndex !== -1) {
        settings.value.base.knockout.splice(existingIndex, 1)
      }
    } else {
      if (existingIndex !== -1) {
        settings.value.base.knockout[existingIndex].b = brightness
      } else {
        settings.value.base.knockout.push({ p: ledIndex, b: brightness })
      }
    }

    const action = { a: 'knockout', p: ledIndex, b: brightness }
    websocketSend(action)
  }

  // Get brightness for a specific LED
  const getKnockoutBrightness = (ledIndex: number): number => {
    if (!settings.value.base?.knockout) return 100
    const knockout = settings.value.base.knockout.find((kp) => kp.p === ledIndex)
    return knockout ? knockout.b : 100
  }

  // Save settings to server
  const saveSettings = async () => {
    if (!hasChanges.value || saving.value) return

    saving.value = true

    if (!settings.value.base) {
      settings.value.base = {}
    }
    settings.value.base.knockout =
      settings.value.base?.knockout?.filter(({ p, b }) => p !== undefined && p !== null && b < 100) ??
      []

    try {
      const response = await fetch(`${import.meta.env.VITE_SERVER_HTTP}/settings`, {
        method: 'PUT',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(settings.value),
      })

      if (response.ok) {
        originalSettings.value = JSON.stringify(settings.value)
      }
      saving.value = false
    } catch (error) {
      console.error('Error saving settings:', error)
    } finally {
      saving.value = false
    }
  }

  // Expression handlers
  const handleTestExpression = (type: string) => {
    websocketSend({ a: 'test_expression', type })
  }

  const handleTestExpressionComplete = () => {
    websocketSend({
      a: 'test_expression_complete',
      shadeColors: settings.value.shade?.colors || [],
      baseColors: settings.value.base?.colors || [],
    })
  }

  const handleExpressionColorPreview = (color: string, target: number) => {
    if (target === 1 || target === 3) {
      websocketSend({ a: 'shade', c: [color] })
    }
    if (target === 2 || target === 3) {
      websocketSend({ a: 'base', c: [color] })
    }
  }

  const handleExpressionColorPickerClose = () => {
    if (settings.value.shade?.colors) {
      websocketSend({ a: 'shade', c: settings.value.shade.colors })
    }
    if (settings.value.base?.colors) {
      websocketSend({ a: 'base', c: settings.value.base.colors })
    }
  }

  const handleSaveAndRestart = async () => {
    await saveSettings()
    resetUnsavedChanges.value++
  }

  // Initialize state (only once)
  const initialize = async () => {
    if (isInitialized) return

    isInitialized = true

    try {
      const response = await fetch(`${import.meta.env.VITE_SERVER_HTTP}/settings`)
      const data = await response.json()
      settings.value = data
      originalSettings.value = JSON.stringify(data)
      loaded.value = true
      connectWebSocket()
    } catch (error) {
      console.error('Error loading settings:', error)
      loaded.value = true // Still mark as loaded so UI can show error state
    }
  }

  // Cleanup function
  const cleanup = () => {
    if (reconnectTimeout) {
      clearTimeout(reconnectTimeout)
      reconnectTimeout = null
    }

    if (websocketDebounceTimeout) {
      clearTimeout(websocketDebounceTimeout)
      websocketDebounceTimeout = null
    }

    if (ws.value) {
      ws.value.close()
      ws.value = null
    }

    wsConnected.value = false
    disabled.value = true
    isInitialized = false
  }

  // Set active tab and notify lamp
  const setActiveTab = (tabId: string) => {
    activeTab.value = tabId
    if (ws.value?.readyState === WebSocket.OPEN) {
      websocketSend({ a: 'tab', v: tabId })
    }
  }

  return {
    // State
    settings,
    loaded,
    disabled,
    saving,
    hasChanges,
    wsConnected,
    activeTab,
    resetUnsavedChanges,

    // Methods
    initialize,
    cleanup,
    updateSetting,
    updateKnockoutPixel,
    getKnockoutBrightness,
    saveSettings,
    setActiveTab,
    websocketSend,

    // Expression handlers
    handleTestExpression,
    handleTestExpressionComplete,
    handleExpressionColorPreview,
    handleExpressionColorPickerClose,
    handleSaveAndRestart,
  }
}

