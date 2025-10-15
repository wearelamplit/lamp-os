<template>
  <div class="color-picker">
    <!-- Color Input Field -->
    <ColorPreview :hex-value="hexwwValue" size="small" :disabled="disabled" @click="openDialog" />

    <!-- Color Picker Dialog -->
    <Teleport to="body">
      <div v-if="isDialogOpen" class="dialog-overlay" @click="closeDialog">
        <div class="dialog-content" @click.stop>
          <div class="dialog-header">
            <h3>Color Picker</h3>
            <button class="close-button" @click="closeDialog">×</button>
          </div>

          <div class="dialog-body">
            <!-- Color Preview -->
            <ColorPreview :hex-value="hexwwValue" size="large" />

            <!-- Hex Input -->
            <div class="input-group">
              <label for="hex-input">Hex Value (RGB or RGBWW):</label>
              <input
                id="hex-input"
                v-model="hexInput"
                type="text"
                class="hex-input"
                placeholder="#FF0000FF or #FF0000"
                @blur="updateFromHex"
              />
            </div>

            <!-- Sliders -->
            <div class="sliders-container">
              <NumberSlider
                v-model="colorValues.red"
                label="Red"
                id="red"
                color="#FF0000"
                @update:model-value="updateColor"
              />
              <NumberSlider
                v-model="colorValues.green"
                label="Green"
                id="green"
                color="#00FF00"
                @update:model-value="updateColor"
              />
              <NumberSlider
                v-model="colorValues.blue"
                label="Blue"
                id="blue"
                color="#0000FF"
                @update:model-value="updateColor"
              />
              <NumberSlider
                v-model="colorValues.warmWhite"
                label="Warm White"
                id="ww"
                color="#FFAA00"
                @update:model-value="updateColor"
              />
            </div>
          </div>

          <!-- Dialog Footer with Buttons -->
          <div class="dialog-footer" @click.stop>
            <button class="btn btn-cancel" @click="cancelDialog">Cancel</button>
            <button class="btn btn-ok" @click="confirmDialog">OK</button>
          </div>
        </div>
      </div>
    </Teleport>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onUnmounted } from 'vue'
import ColorPreview from './ColorPreview.vue'
import NumberSlider from './NumberSlider.vue'

interface ColorValues {
  red: number
  green: number
  blue: number
  warmWhite: number
}

const props = defineProps<{
  modelValue: string
  disabled?: boolean
}>()

const emit = defineEmits<{
  'update:modelValue': [value: string]
  'preview': [value: string]
  'open': []
  'close': []
}>()

const isDialogOpen = ref(false)
const hexInput = ref('')
const originalColor = ref('')

const colorValues = ref<ColorValues>({
  red: 255,
  green: 0,
  blue: 0,
  warmWhite: 255,
})

// Computed properties
const hexwwValue = computed(() => {
  const { red, green, blue, warmWhite } = colorValues.value
  return `#${red.toString(16).padStart(2, '0')}${green.toString(16).padStart(2, '0')}${blue.toString(16).padStart(2, '0')}${warmWhite.toString(16).padStart(2, '0')}`.toUpperCase()
})

// Methods
const openDialog = () => {
  if (props.disabled) return
  isDialogOpen.value = true
  emit('open')
  // Store the original color before making changes
  originalColor.value = props.modelValue || '#FF0000FF'
  // Initialize the hex input with the current model value
  hexInput.value = props.modelValue || '#FF0000FF'
  // Parse the hex value to update sliders without emitting
  parseHexwwValue(props.modelValue)

  // Prevent body scroll
  document.body.style.overflow = 'hidden'

  // Add escape key listener
  document.addEventListener('keydown', handleEscapeKey)
}

const closeDialog = () => {
  // Reset to original color when closing without confirmation
  parseHexwwValue(originalColor.value)
  hexInput.value = originalColor.value
  emit('update:modelValue', originalColor.value)

  emit('close')

  isDialogOpen.value = false
  // Re-enable body scroll
  document.body.style.overflow = ''
  // Remove escape key listener
  document.removeEventListener('keydown', handleEscapeKey)
}

const handleEscapeKey = (event: KeyboardEvent) => {
  if (event.key === 'Escape') {
    closeDialog()
  }
}

const cancelDialog = () => {
  // Reset to original color
  parseHexwwValue(originalColor.value)
  // Update hex input to match
  hexInput.value = originalColor.value
  // Emit the original value to reset parent
  emit('update:modelValue', originalColor.value)

  emit('close')

  isDialogOpen.value = false
  // Re-enable body scroll
  document.body.style.overflow = ''
  // Remove escape key listener
  document.removeEventListener('keydown', handleEscapeKey)
}

const confirmDialog = () => {
  // Ensure the current color is emitted before closing
  emit('update:modelValue', hexwwValue.value)

  emit('close')

  isDialogOpen.value = false
  // Re-enable body scroll
  document.body.style.overflow = ''
  // Remove escape key listener
  document.removeEventListener('keydown', handleEscapeKey)
}

const parseHexwwValue = (value: string) => {
  if (!value) return

  const hex = value.replace('#', '')
  if (hex.length === 8) {
    colorValues.value = {
      red: parseInt(hex.substring(0, 2), 16),
      green: parseInt(hex.substring(2, 4), 16),
      blue: parseInt(hex.substring(4, 6), 16),
      warmWhite: parseInt(hex.substring(6, 8), 16),
    }
  } else if (hex.length === 6) {
    colorValues.value = {
      red: parseInt(hex.substring(0, 2), 16),
      green: parseInt(hex.substring(2, 4), 16),
      blue: parseInt(hex.substring(4, 6), 16),
      warmWhite: 0, // Assume 00 for warm white when not specified
    }
  }
}

const updateColor = () => {
  const newValue = hexwwValue.value
  // Emit real-time updates when dialog is open
  if (isDialogOpen.value) {
    emit('update:modelValue', newValue)
    // Emit preview event for live LED preview while adjusting
    emit('preview', newValue)
  }
  // Update the hex input to reflect the slider changes
  hexInput.value = newValue
}

const updateFromHex = () => {
  const input = hexInput.value.trim()

  // Allow any input, but only validate and update if it's a proper hex format
  if (input.startsWith('#')) {
    const hex = input.substring(1)

    // Check if it's a valid hex string (6 or 8 characters, all hex digits)
    if (hex.length === 6 || hex.length === 8) {
      const isValidHex = /^[0-9A-Fa-f]+$/.test(hex)
      if (isValidHex) {
        parseHexwwValue(input)
        // Emit real-time updates when dialog is open
        if (isDialogOpen.value) {
          emit('update:modelValue', hexwwValue.value)
        }
        // Update the hex input to reflect the parsed value
        hexInput.value = hexwwValue.value
      }
    }
  } else if (input.length === 6 || input.length === 8) {
    // Handle hex without # prefix
    const isValidHex = /^[0-9A-Fa-f]+$/.test(input)
    if (isValidHex) {
      parseHexwwValue(`#${input}`)
      // Emit real-time updates when dialog is open
      if (isDialogOpen.value) {
        emit('update:modelValue', hexwwValue.value)
      }
      // Update the hex input to reflect the parsed value
      hexInput.value = hexwwValue.value
    }
  }
}

// Watch for external changes
watch(
  () => props.modelValue,
  (newValue) => {
    if (newValue && !isDialogOpen.value) {
      parseHexwwValue(newValue)
    }
  },
  { immediate: true },
)

// Cleanup event listeners when component is unmounted
onUnmounted(() => {
  document.removeEventListener('keydown', handleEscapeKey)
  // Re-enable body scroll if dialog was open
  if (isDialogOpen.value) {
    document.body.style.overflow = ''
  }
})
</script>

<style scoped>
.color-picker {
  position: relative;
  width: 100%;
}

/* Dialog Styles */
.dialog-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-color: rgba(0, 0, 0, 0.8);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
  padding: 20px;
  backdrop-filter: blur(4px);
}

.dialog-content {
  background-color: var(--color-background-mute);
  border-radius: 20px;
  box-shadow: 0 25px 50px rgba(0, 0, 0, 0.5);
  width: 100%;
  max-width: 450px;
  max-height: 90vh;
  overflow-y: auto;
  border: 1px solid var(--color-border);
}

.dialog-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 24px 24px 0 24px;
  border-bottom: 1px solid var(--color-border);
  padding-bottom: 20px;
}

.dialog-header h3 {
  margin: 0;
  font-size: 20px;
  font-weight: 700;
  color: var(--brand-lamp-white);
}

.close-button {
  background: var(--color-background-soft);
  border: 1px solid var(--color-border);
  font-size: 20px;
  cursor: pointer;
  color: var(--brand-fog-grey);
  padding: 0;
  width: 36px;
  height: 36px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  transition: all 0.2s ease;
}

.close-button:hover {
  background-color: var(--color-background-mute);
  color: var(--brand-lamp-white);
  transform: scale(1.05);
}

.dialog-body {
  padding: 24px;
}

.input-group {
  margin-bottom: 24px;
}

.input-group label {
  display: block;
  margin-bottom: 10px;
  font-weight: 600;
  color: var(--brand-lamp-white);
  font-size: 14px;
}

.hex-input {
  width: 100%;
  padding: 14px 16px;
  border: 2px solid var(--color-border);
  border-radius: 12px;
  font-family: 'JetBrains Mono', 'Courier New', monospace;
  font-size: 14px;
  font-weight: 500;
  transition: all 0.2s ease;
  background-color: var(--color-background-mute);
  color: var(--brand-lamp-white);
}

.hex-input:focus {
  outline: none;
  border-color: var(--brand-aurora-blue);
  background-color: var(--color-background-mute);
  box-shadow: 0 0 0 3px rgba(68, 108, 156, 0.2);
}

.sliders-container {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  padding: 20px 24px 24px 24px;
  border-top: 1px solid var(--color-border);
}

.btn {
  padding: 12px 24px;
  border-radius: 10px;
  font-weight: 600;
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s ease;
  border: none;
}

.btn-cancel {
  background-color: var(--color-background-soft);
  color: var(--brand-fog-grey);
  border: 1px solid var(--color-border);
}

.btn-cancel:hover {
  background-color: var(--color-background-mute);
  color: var(--brand-lamp-white);
  transform: translateY(-1px);
}

.btn-ok {
  background-color: var(--brand-aurora-blue);
  color: var(--brand-lamp-white);
}

.btn-ok:hover {
  background-color: var(--color-accent-hover);
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(68, 108, 156, 0.3);
}

/* Mobile-first responsive design */
@media (max-width: 480px) {
  .dialog-content {
    margin: 10px;
    max-width: calc(100vw - 20px);
    border-radius: 16px;
  }

  .dialog-body {
    padding: 20px;
  }

  .dialog-footer {
    padding: 16px 20px 20px 20px;
  }

  .dialog-header {
    padding: 20px 20px 0 20px;
    padding-bottom: 16px;
  }

  .hex-input {
    font-size: 13px;
    padding: 12px 14px;
  }
}
</style>
