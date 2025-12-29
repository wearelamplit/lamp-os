<script setup lang="ts">
import BrightnessSlider from '@/components/BrightnessSlider.vue'
import NumberInput from '@/components/NumberInput.vue'
import TextInput from '@/components/TextInput.vue'
import BooleanInput from '@/components/BooleanInput.vue'
import FormField from '@/components/FormField.vue'
import InfoPanel from '@/components/InfoPanel.vue'
import { useLampState, MAX_LEDS_BASE } from '@/composables/useLampState'

const { settings, disabled, updateSetting, updateKnockoutPixel, getKnockoutBrightness } =
  useLampState()
</script>

<template>
  <section class="tab-panel" aria-label="Setup settings">
    <h1 class="gold">Lamp Name</h1>
    <FormField id="name">
      <TextInput
        :model-value="settings.lamp?.name || ''"
        @update:model-value="(value) => updateSetting('lamp.name', value)"
        placeholder="Enter a name for your lamp"
        :disabled="disabled"
        :max-length="12"
        pattern="[a-z]+"
        transform="lowercase"
      />
      <InfoPanel>
        Names must be all lowercase letters and between 3-12 characters.
      </InfoPanel>
    </FormField>

    <h1 class="yellow">Lamp Password</h1>
    <FormField id="password">
      <TextInput
        :model-value="settings.lamp?.password || ''"
        @update:model-value="(value) => updateSetting('lamp.password', value)"
        placeholder="Optional password"
        :disabled="disabled"
        pattern="[ -~]+"
        :max-length="16"
      />
      <InfoPanel>
        Optional password to protect your lamp from changes. Between 8-16 characters. Leave empty
        for no password.
      </InfoPanel>
    </FormField>

    <h1 class="lime">At-Home Mode</h1>
    <div class="mode-toggles">
      <FormField label="Home Mode" id="homeMode">
        <BooleanInput
          :model-value="settings.lamp?.homeMode || false"
          @update:model-value="(value) => updateSetting('lamp.homeMode', value)"
          :disabled="disabled"
        />
      </FormField>

      <!-- Home Mode Settings -->
      <div v-if="settings.lamp?.homeMode" class="home-mode-settings">
        <FormField label="Home Mode Brightness" id="homeModeBrightness">
          <BrightnessSlider
            :model-value="settings.lamp?.homeModeBrightness ?? 80"
            @update:model-value="(value) => updateSetting('lamp.homeModeBrightness', value)"
            id="homeModeBrightness"
            :min="0"
            :max="100"
            append="%"
            :disabled="disabled"
          />
        </FormField>

        <FormField label="Home Network SSID" id="homeModeSSID">
          <TextInput
            :model-value="settings.lamp?.homeModeSSID || ''"
            @update:model-value="(value) => updateSetting('lamp.homeModeSSID', value)"
            placeholder="Enter your home WiFi name"
            :disabled="disabled"
            :max-length="32"
            pattern="[ -~]+"
          />
          <InfoPanel>
            When the lamp detects this WiFi network, it will automatically activate special
            home-only features and behaviors.
          </InfoPanel>
        </FormField>
      </div>
    </div>

    <h1 class="green">Lamp Base LED Profile</h1>
    <FormField label="Base LED Count" id="baseLeds">
      <NumberInput
        :model-value="settings.base?.px || 36"
        @update:model-value="(value) => updateSetting('base.px', value)"
        :min="5"
        :max="MAX_LEDS_BASE"
        placeholder="Number of LEDs"
        :disabled="disabled"
      />
    </FormField>

    <FormField label="Per-Pixel Brightness Adjustment" id="baseKnockoutPixels" expandable>
      <div class="pixel-grid">
        <div
          v-for="ledIndex in Array.from(
            { length: settings.base?.px || 36 },
            (_, i) => (settings.base?.px || 36) - i,
          )"
          :key="ledIndex - 1"
          class="pixel-row"
        >
          <label class="pixel-label">LED {{ ledIndex }}</label>
          <BrightnessSlider
            :model-value="getKnockoutBrightness(ledIndex - 1)"
            @update:model-value="(value) => updateKnockoutPixel(ledIndex - 1, value)"
            :id="`knockout-pixel-${ledIndex - 1}`"
            :min="0"
            :max="100"
            append="%"
            :disabled="disabled"
          />
        </div>
      </div>
    </FormField>
  </section>
</template>

<style scoped>
.tab-panel {
  animation: fadeIn 0.3s ease-in-out;
}

@keyframes fadeIn {
  from {
    opacity: 0;
    transform: translateY(10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

/* Mode Toggles Styles */
.mode-toggles {
  display: flex;
  flex-direction: column;
  gap: 0;
}

.mode-toggles > .form-field {
  width: 100%;
}

/* Home Mode SSID Styles */
.home-mode-settings {
  animation: fadeIn 0.3s ease-in-out;
}

.home-mode-settings .form-field {
  margin-top: 8px;
  margin-bottom: 32px;
}

/* Knockout Pixels Styles */
.pixel-grid {
  display: flex;
  flex-direction: column;
  gap: 10px;
  max-height: 400px;
  overflow-y: auto;
  padding: 8px;
  background: rgba(253, 253, 253, 0.02);
  border-radius: 8px;
  border: 1px solid var(--color-border);
}

.pixel-row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 8px;
  background: rgba(253, 253, 253, 0.02);
}

.pixel-label {
  min-width: 80px;
  font-size: 0.9rem;
  color: var(--brand-fog-grey);
  font-weight: 500;
}

.pixel-row .number-slider {
  flex: 1;
}
</style>

