<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { useRouter } from 'vue-router'
import ComponentForm from '@/components/Form.vue'
import type { FieldDefinition, FormValues } from '@/types'

const router = useRouter()

// Example field definitions showcasing all field types
const fields = ref<FieldDefinition[]>([
  {
    name: 'username',
    type: 'text',
    label: 'Username',
    help: 'Enter your username (lowercase letters only)',
    default: '',
    optional: true,
    props: {
      placeholder: 'Enter username',
      maxLength: 20,
      minLength: 3,
      pattern: '[a-z]+',
      transform: 'lowercase',
    },
  },
  {
    name: 'password',
    type: 'password',
    label: 'Password',
    help: 'Enter a secure password (8-32 characters)',
    default: '',
    optional: true,
    props: {
      placeholder: 'Enter password',
      maxLength: 32,
      minLength: 8,
    },
  },
  {
    name: 'isEnabled',
    type: 'boolean',
    label: 'Enable Feature',
    help: 'Toggle this feature on or off',
    default: false,
    optional: true,
  },
  {
    name: 'customSlot',
    type: 'slot',
    label: 'Custom Slot',
  },
  {
    name: 'quantity',
    type: 'number',
    label: 'Quantity',
    help: 'Select a quantity between 1 and 100',
    default: 10,
    optional: true,
    props: {
      min: 1,
      max: 100,
      placeholder: 'Enter quantity',
    },
  },
  {
    name: 'hue',
    type: 'number-slider',
    label: 'Hue',
    help: 'Adjust the hue angle (0-360°)',
    default: 180,
    optional: true,
    props: {
      min: 0,
      max: 360,
      step: 1,
      append: '°',
      color: 'var(--brand-glow-pink)',
    },
  },
  {
    name: 'brightness',
    type: 'brightness-slider',
    label: 'Brightness',
    help: 'Adjust the brightness level (dynamic color indicator)',
    default: 75,
    optional: true,
    props: {
      min: 0,
      max: 100,
      step: 1,
      append: '%',
    },
  },
  {
    name: 'temperatureRange',
    type: 'number-range-slider',
    label: 'Temperature Range',
    help: 'Set the min and max temperature',
    default: [20, 80],
    optional: true,
    transform: ['Min', 'Max'],
    props: {
      min: 0,
      max: 100,
      step: 5,
      append: '°C',
      color: 'var(--brand-glow-pink)',
    },
  },
  {
    name: 'primaryColor',
    type: 'color',
    label: 'Primary Color',
    help: 'Choose your primary color (RGBWW format)',
    default: '#FF5500FF',
    optional: true,
  },
  {
    name: 'colorPalette',
    type: 'color-list',
    label: 'Color Palette',
    help: 'Create a color palette with up to 5 colors',
    default: ['#FF0000FF', '#00FF00FF', '#0000FFFF'],
    optional: true,
    props: {
      max: 5,
      showAddButton: true,
    },
  },
  {
    name: 'hiddenId',
    type: 'hidden',
    label: 'Hidden ID',
    default: 'demo-form-123',
    optional: true,
  },
])

// Predefined form values to test that initial values are properly loaded
// Note: temperatureRange uses transformed format (Min/Max suffix) as defined in field.transform
const formValues = ref<FormValues>({
  username: 'testuser',
  password: 'chicken',
  isEnabled: true,
  quantity: 42,
  hue: 270,
  brightness: 85,
  temperatureRangeMin: 15,
  temperatureRangeMax: 65,
  primaryColor: '#00AAFFFF',
  colorPalette: ['#FF6600FF', '#00FF99FF', '#9933FFFF', '#FFCC00FF'],
  hiddenId: 'preset-demo-456',
})

watch(formValues, (newValues) => {
  console.log('Form values changed:', newValues)
})

// Submitted values for display
const submittedValues = ref<FormValues | null>(null)

// Format submitted values for display
const formattedSubmittedValues = computed(() => {
  if (!submittedValues.value) return ''
  return JSON.stringify(submittedValues.value, null, 2)
})

// Handle form submission
const handleSubmit = (values: FormValues) => {
  console.log('Form submitted:', JSON.stringify(values, null, 2))
  submittedValues.value = values
}

// Go back to home
const goHome = () => {
  router.push('/')
}

// Initial values for reset functionality (using transformed format for range fields)
const initialValues: FormValues = {
  username: 'testuser',
  password: 'chicken',
  isEnabled: true,
  quantity: 42,
  hue: 270,
  brightness: 85,
  temperatureRangeMin: 15,
  temperatureRangeMax: 65,
  primaryColor: '#00AAFFFF',
  colorPalette: ['#FF6600FF', '#00FF99FF', '#9933FFFF', '#FFCC00FF'],
  hiddenId: 'preset-demo-456',
}

// Reset form to initial values
const resetForm = () => {
  submittedValues.value = null
  formValues.value = { ...initialValues }
}
</script>

<template>
  <div class="form-demo-page">
    <div class="container">
      <header class="page-header">
        <button class="back-button" @click="goHome" title="Back to Home">
          ← Back
        </button>
        <h1>Form Demo</h1>
        <p class="subtitle">Dynamic form generation with all field types</p>
      </header>

      <main class="main-content">
        <ComponentForm
          :fields="fields"
          v-model="formValues"
          @submit="handleSubmit"
          button-text="Optional Button"
          :show-button="true"
        >
          <!-- Custom slot content -->
          <template #customSlot>
            <div style="color: var(--brand-aurora-blue);">
              This is custom slot content passed to the form.
            </div>
          </template>
        </ComponentForm>

        <!-- Submitted Values Display -->
        <div v-if="submittedValues" class="submitted-values">
          <div class="submitted-header">
            <h2>Submitted Values</h2>
            <button class="reset-button" @click="resetForm">Reset</button>
          </div>
          <pre class="values-display">{{ formattedSubmittedValues }}</pre>
        </div>
      </main>
    </div>
  </div>
</template>

<style scoped>
.form-demo-page {
  min-height: 100vh;
  background: var(--brand-midnight-black);
  padding: 16px;
  width: 100%;
}

.container {
  width: 100%;
  max-width: 500px;
  margin: 0 auto;
}

.page-header {
  margin-bottom: 24px;
  text-align: center;
}

.back-button {
  position: absolute;
  left: 16px;
  top: 16px;
  padding: 8px 16px;
  background: var(--color-background-soft);
  border: 1px solid var(--color-border);
  border-radius: 8px;
  color: var(--brand-fog-grey);
  font-size: 0.9rem;
  cursor: pointer;
  transition: all 0.2s ease;
}

.back-button:hover {
  background: var(--color-background-mute);
  color: var(--brand-lamp-white);
}

.page-header h1 {
  color: var(--brand-lamp-white);
  font-size: 1.5rem;
  font-weight: 700;
  margin-bottom: 8px;
}

.subtitle {
  color: var(--brand-fog-grey);
  font-size: 0.9rem;
}

.main-content {
  background: var(--color-background-soft);
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
}

.submitted-values {
  margin-top: 32px;
  padding-top: 24px;
  border-top: 1px solid var(--color-border);
}

.submitted-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.submitted-header h2 {
  color: var(--brand-lumen-green);
  font-size: 1.1rem;
  font-weight: 600;
  margin: 0;
}

.reset-button {
  padding: 6px 12px;
  background: var(--color-background-mute);
  border: 1px solid var(--color-border);
  border-radius: 6px;
  color: var(--brand-fog-grey);
  font-size: 0.8rem;
  cursor: pointer;
  transition: all 0.2s ease;
}

.reset-button:hover {
  background: var(--color-error);
  border-color: var(--color-error);
  color: var(--brand-lamp-white);
}

.values-display {
  background: var(--color-background-mute);
  padding: 16px;
  border-radius: 8px;
  overflow-x: auto;
  font-family: 'JetBrains Mono', 'Courier New', monospace;
  font-size: 0.85rem;
  line-height: 1.5;
  color: var(--brand-lamp-white);
  margin: 0;
  white-space: pre-wrap;
  word-break: break-word;
}

/* Mobile optimizations */
@media (max-width: 480px) {
  .form-demo-page {
    padding: 12px;
  }

  .main-content {
    padding: 16px;
  }

  .back-button {
    left: 12px;
    top: 12px;
    padding: 6px 12px;
    font-size: 0.8rem;
  }

  .page-header {
    padding-top: 40px;
  }
}
</style>

