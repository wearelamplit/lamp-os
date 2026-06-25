<template>
  <main class="app">
    <h1>Lamp config</h1>

    <p v-if="saved" class="saved">Saved. Lamp is rebooting.</p>

    <template v-else>
      <CritterNameplate :model-value="nameplateModel" />

      <FormField label="Name" id="name" required>
        <TextInput v-model="name" :max-length="12" placeholder="lamp name" />
      </FormField>

      <FormField label="Shade color" id="shade">
        <ColorPicker v-model="shadeColors[0]" />
      </FormField>

      <FormField label="Base color (gradient)" id="base">
        <ColorGradient v-model="baseColors" :maxColors="5" />
      </FormField>

      <FormField label="Brightness" id="brightness">
        <BrightnessSlider v-model="brightness" id="brightness" />
      </FormField>

      <button class="save" :disabled="saving || !ready" @click="save">
        {{ saving ? 'Saving…' : 'Save' }}
      </button>
    </template>
  </main>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import BrightnessSlider from './components/BrightnessSlider.vue'
import ColorGradient from './components/ColorGradient.vue'
import ColorPicker from './components/ColorPicker.vue'
import CritterNameplate from './components/CritterNameplate.vue'
import FormField from './components/FormField.vue'
import TextInput from './components/TextInput.vue'

const name = ref('')
const baseColors = ref<string[]>(['#FF0000FF'])
const shadeColors = ref<string[]>(['#FF0000FF'])
const brightness = ref(100)
const ready = ref(false)
const saving = ref(false)
const saved = ref(false)

const nameplateModel = computed(() => ({ lamp: { name: name.value } }))

let ws: WebSocket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

const connectWs = () => {
  if (saved.value) return
  ws = new WebSocket(`ws://${location.host}/ws`)
  ws.onclose = () => {
    if (saved.value) return
    reconnectTimer = setTimeout(connectWs, 1000)
  }
}

const sendPreview = (payload: Record<string, string[] | number>) => {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload))
  }
}

watch(
  baseColors,
  (value) => {
    if (!ready.value) return
    sendPreview({ baseColors: [...value] })
  },
  { deep: true },
)

watch(
  shadeColors,
  (value) => {
    if (!ready.value) return
    sendPreview({ shadeColors: [...value] })
  },
  { deep: true },
)

watch(brightness, (value) => {
  if (!ready.value) return
  sendPreview({ brightness: value })
})

const save = async () => {
  if (saving.value) return
  saving.value = true
  try {
    const res = await fetch('/api/settings', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        name: name.value,
        baseColors: baseColors.value,
        shadeColors: shadeColors.value,
        brightness: brightness.value,
      }),
    })
    if (res.ok) {
      saved.value = true
      if (ws) {
        ws.onclose = null
        ws.close()
        ws = null
      }
      if (reconnectTimer) {
        clearTimeout(reconnectTimer)
        reconnectTimer = null
      }
    } else {
      saving.value = false
    }
  } catch {
    saving.value = false
  }
}

onMounted(async () => {
  try {
    const res = await fetch('/api/settings')
    const s = await res.json()
    name.value = s.name ?? ''
    baseColors.value = Array.isArray(s.baseColors) && s.baseColors.length > 0
      ? s.baseColors
      : ['#FF0000FF']
    shadeColors.value = Array.isArray(s.shadeColors) && s.shadeColors.length > 0
      ? s.shadeColors
      : ['#FF0000FF']
    brightness.value = typeof s.brightness === 'number' ? s.brightness : 100
  } catch {
    // Leave defaults; user can still save fresh values.
  }
  ready.value = true
  connectWs()
})

onUnmounted(() => {
  if (ws) ws.close()
  if (reconnectTimer) clearTimeout(reconnectTimer)
})
</script>

<style scoped>
.app {
  max-width: 28em;
  margin: 0 auto;
  padding: 1.5em 1em 3em;
  display: flex;
  flex-direction: column;
}

h1 {
  font-size: 1.4rem;
  font-weight: 800;
  margin-bottom: 0.5em;
  color: var(--brand-lamp-white);
}

.save {
  margin-top: 1em;
  padding: 14px 16px;
  font-size: 1rem;
  font-weight: 700;
  border: 0;
  border-radius: 12px;
  background: var(--brand-aurora-blue);
  color: var(--brand-lamp-white);
  cursor: pointer;
  transition: background 0.2s ease;
}

.save:hover:not(:disabled) {
  background: var(--color-accent-hover);
}

.save:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.saved {
  font-size: 1.1rem;
  color: var(--brand-lumen-green);
  text-align: center;
  margin-top: 2em;
}
</style>
