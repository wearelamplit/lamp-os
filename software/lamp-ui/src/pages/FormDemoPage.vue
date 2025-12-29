<script setup lang="ts">
import { ref, computed } from 'vue'
import { useRouter } from 'vue-router'
import DynamicForm from '@/components/Form.vue'
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
    name: 'isEnabled',
    type: 'boolean',
    label: 'Enable Feature',
    help: 'Toggle this feature on or off',
    default: false,
    optional: true,
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
    name: 'brightness',
    type: 'number-slider',
    label: 'Brightness',
    help: 'Adjust the brightness level',
    default: 75,
    optional: true,
    props: {
      min: 0,
      max: 100,
      step: 1,
      append: '%',
      color: 'var(--brand-amber-gold)',
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
  {
    name: 'customSlot',
    type: 'slot',
    label: 'Custom Slot',
  },
])

// Form values
const formValues = ref<FormValues>({})

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

// Reset form
const resetForm = () => {
  submittedValues.value = null
  formValues.value = {}
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
        <DynamicForm
          :fields="fields"
          v-model="formValues"
          @submit="handleSubmit"
          button-text="Submit Form"
        >
          <!-- Custom slot content -->
          <template #customSlot>
            <div class="custom-slot-content">
              <p>This is custom slot content passed to the form.</p>
              <p>You can put any Vue component or HTML here.</p>
            </div>
          </template>
        </DynamicForm>

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

.custom-slot-content {
  padding: 16px;
  background: rgba(68, 108, 156, 0.1);
  border-radius: 8px;
  border-left: 3px solid var(--brand-aurora-blue);
  margin: 8px 0 24px 0;
}

.custom-slot-content p {
  margin: 0 0 8px 0;
  font-size: 0.9rem;
  color: var(--brand-fog-grey);
}

.custom-slot-content p:last-child {
  margin-bottom: 0;
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

