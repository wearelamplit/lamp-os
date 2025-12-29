/**
 * Lamp Store
 *
 * Centralized state management for lamp settings, WebSocket connection,
 * and API communication using Pinia.
 */

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

// Configuration constants
const MAX_RECONNECT_ATTEMPTS = 60
const RECONNECT_INTERVAL = 2500
const WEBSOCKET_DEBOUNCE_INTERVAL = 10
export const MAX_LEDS_BASE = 50

// Types
interface KnockoutPixel {
  p: number
  b: number
}

interface LampSettings {
  name?: string
  brightness?: number
  homeMode?: boolean
  homeModeSSID?: string
  homeModeBrightness?: number
  password?: string
}

interface ShadeSettings {
  px?: number
  colors?: string[]
}

interface BaseSettings {
  px?: number
  colors?: string[]
  ac?: number
  knockout?: KnockoutPixel[]
}

interface ExpressionSettings {
  type: string
  enabled: boolean
  target: number
  colors: (string | null)[]
  intervalMin?: number
  intervalMax?: number
  durationMin?: number
  durationMax?: number
  shiftDurationMin?: number
  shiftDurationMax?: number
  fadeDuration?: number
  pulseSpeed?: number
}

export interface LampState {
  lamp?: LampSettings
  shade?: ShadeSettings
  base?: BaseSettings
  expressions?: ExpressionSettings[]
}

// Tab configuration
export const tabs = [
  { id: 'home', label: 'Home', path: '/' },
  { id: 'expressions', label: 'Expressions', path: '/expressions' },
  { id: 'lamp-setup', label: 'Setup', path: '/setup' },
  { id: 'info', label: 'Info', path: '/info' },
]

export const useLampStore = defineStore('lamp', () => {
  // State
  const state = ref<LampState>({})
  const originalState = ref<string>('')
  const loaded = ref(false)
  const saving = ref(false)
  const activeTab = ref('home')

  // WebSocket state
  const ws = ref<WebSocket | null>(null)
  const wsConnected = ref(false)
  const reconnectAttempts = ref(0)
  let reconnectTimeout: number | null = null
  let websocketDebounceTimeout: number | null = null

  // Computed
  const hasChanges = computed(() => {
    return JSON.stringify(state.value) !== originalState.value
  })

  const disabled = computed(() => !wsConnected.value)

  // WebSocket Methods
  const websocketSend = (action: Record<string, unknown>) => {
    if (websocketDebounceTimeout) {
      clearTimeout(websocketDebounceTimeout)
    }

    websocketDebounceTimeout = window.setTimeout(() => {
      ws.value?.send(JSON.stringify(action))
      websocketDebounceTimeout = null
    }, WEBSOCKET_DEBOUNCE_INTERVAL)
  }

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
      reconnectAttempts.value = 0

      // Send current brightness based on home mode state
      if (state.value.lamp) {
        const brightness = state.value.lamp.homeMode
          ? (state.value.lamp.homeModeBrightness ?? 80)
          : (state.value.lamp.brightness ?? 100)
        websocketSend({ a: 'bright', v: brightness })
      }

      // Send current tab state
      websocketSend({ a: 'tab', v: activeTab.value })

      // Send current colors to establish preview
      if (state.value.shade?.colors) {
        websocketSend({ a: 'shade', c: state.value.shade.colors })
      }
      if (state.value.base?.colors) {
        websocketSend({ a: 'base', c: state.value.base.colors })
      }
    }

    ws.value.onclose = () => {
      wsConnected.value = false
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
    }

    return ws.value
  }

  // State Update Methods - Named clearly for their purpose
  const updateLampName = (name: string) => {
    if (!state.value.lamp) state.value.lamp = {}
    state.value.lamp.name = name
  }

  const updateLampPassword = (password: string) => {
    if (!state.value.lamp) state.value.lamp = {}
    state.value.lamp.password = password
  }

  const updateBrightness = (brightness: number) => {
    if (!state.value.lamp) state.value.lamp = {}
    state.value.lamp.brightness = brightness
    if (!state.value.lamp.homeMode) {
      websocketSend({ a: 'bright', v: brightness })
    }
  }

  const updateHomeMode = (enabled: boolean) => {
    if (!state.value.lamp) state.value.lamp = {}
    state.value.lamp.homeMode = enabled

    if (enabled) {
      websocketSend({ a: 'bright', v: state.value.lamp.homeModeBrightness ?? 80 })
    } else {
      websocketSend({ a: 'bright', v: state.value.lamp.brightness ?? 100 })
    }
  }

  const updateHomeModeBrightness = (brightness: number) => {
    if (!state.value.lamp) state.value.lamp = {}
    state.value.lamp.homeModeBrightness = brightness
    if (state.value.lamp.homeMode) {
      websocketSend({ a: 'bright', v: brightness })
    }
  }

  const updateHomeModeSSID = (ssid: string) => {
    if (!state.value.lamp) state.value.lamp = {}
    state.value.lamp.homeModeSSID = ssid
  }

  const updateShadeColors = (colors: string[]) => {
    if (!state.value.shade) state.value.shade = {}
    state.value.shade.colors = colors
    websocketSend({ a: 'shade', c: colors })
  }

  const updateBaseColors = (colors: string[]) => {
    if (!state.value.base) state.value.base = {}
    state.value.base.colors = colors
    websocketSend({ a: 'base', c: colors })
  }

  const updateBaseActiveColor = (index: number) => {
    if (!state.value.base) state.value.base = {}
    state.value.base.ac = index
  }

  const updateBasePxCount = (count: number) => {
    if (!state.value.base) state.value.base = {}
    state.value.base.px = count
  }

  const updateKnockoutPixel = (ledIndex: number, brightness: number) => {
    if (!state.value.base) state.value.base = {}
    if (!state.value.base.knockout) state.value.base.knockout = []

    const existingIndex = state.value.base.knockout.findIndex((kp) => kp.p === ledIndex)

    if (brightness === 100) {
      if (existingIndex !== -1) {
        state.value.base.knockout.splice(existingIndex, 1)
      }
    } else {
      if (existingIndex !== -1) {
        state.value.base.knockout[existingIndex].b = brightness
      } else {
        state.value.base.knockout.push({ p: ledIndex, b: brightness })
      }
    }

    websocketSend({ a: 'knockout', p: ledIndex, b: brightness })
  }

  const getKnockoutBrightness = (ledIndex: number): number => {
    if (!state.value.base?.knockout) return 100
    const knockout = state.value.base.knockout.find((kp) => kp.p === ledIndex)
    return knockout ? knockout.b : 100
  }

  const updateExpressions = (expressions: ExpressionSettings[]) => {
    state.value.expressions = expressions
  }

  // Expression handlers
  const testExpression = (type: string) => {
    websocketSend({ a: 'test_expression', type })
  }

  const testExpressionComplete = () => {
    websocketSend({
      a: 'test_expression_complete',
      shadeColors: state.value.shade?.colors || [],
      baseColors: state.value.base?.colors || [],
    })
  }

  const previewExpressionColor = (color: string, target: number) => {
    if (target === 1 || target === 3) {
      websocketSend({ a: 'shade', c: [color] })
    }
    if (target === 2 || target === 3) {
      websocketSend({ a: 'base', c: [color] })
    }
  }

  const restoreColorsAfterPreview = () => {
    if (state.value.shade?.colors) {
      websocketSend({ a: 'shade', c: state.value.shade.colors })
    }
    if (state.value.base?.colors) {
      websocketSend({ a: 'base', c: state.value.base.colors })
    }
  }

  // Tab management
  const setActiveTab = (tabId: string) => {
    activeTab.value = tabId
    if (ws.value?.readyState === WebSocket.OPEN) {
      websocketSend({ a: 'tab', v: tabId })
    }
  }

  // Save settings
  const saveSettings = async () => {
    if (!hasChanges.value || saving.value) return

    saving.value = true

    // Clean up knockout pixels
    if (!state.value.base) state.value.base = {}
    state.value.base.knockout =
      state.value.base?.knockout?.filter(
        ({ p, b }) => p !== undefined && p !== null && b < 100
      ) ?? []

    try {
      const response = await fetch(`${import.meta.env.VITE_SERVER_HTTP}/settings`, {
        method: 'PUT',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(state.value),
      })

      if (response.ok) {
        originalState.value = JSON.stringify(state.value)
      }
    } catch (error) {
      console.error('Error saving settings:', error)
    } finally {
      saving.value = false
    }
  }

  // Reset to original state
  const resetState = () => {
    if (originalState.value) {
      state.value = JSON.parse(originalState.value)
    }
  }

  // Initialize store
  const initialize = async () => {
    try {
      const response = await fetch(`${import.meta.env.VITE_SERVER_HTTP}/settings`)
      const data = await response.json()
      state.value = data
      originalState.value = JSON.stringify(data)
      loaded.value = true
      connectWebSocket()
    } catch (error) {
      console.error('Error loading settings:', error)
      loaded.value = true // Still mark as loaded so UI can show error state
    }
  }

  // Cleanup
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
  }

  return {
    // State
    state,
    loaded,
    saving,
    wsConnected,
    disabled,
    hasChanges,
    activeTab,

    // Lamp update methods
    updateLampName,
    updateLampPassword,
    updateBrightness,
    updateHomeMode,
    updateHomeModeBrightness,
    updateHomeModeSSID,

    // Color update methods
    updateShadeColors,
    updateBaseColors,
    updateBaseActiveColor,

    // Base/LED methods
    updateBasePxCount,
    updateKnockoutPixel,
    getKnockoutBrightness,

    // Expression methods
    updateExpressions,
    testExpression,
    testExpressionComplete,
    previewExpressionColor,
    restoreColorsAfterPreview,

    // Navigation
    setActiveTab,

    // Actions
    saveSettings,
    resetState,
    initialize,
    cleanup,
    websocketSend,
  }
})

