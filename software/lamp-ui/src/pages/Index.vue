<script setup lang="ts">
import ColorGradient from '@/components/ColorGradient.vue'
import BrightnessSlider from '@/components/BrightnessSlider.vue'
import FormField from '@/components/FormField.vue'
import CritterNameplate from '@/components/CritterNameplate.vue'
import { useLampState } from '@/composables/useLampState'

const { settings, disabled, updateSetting } = useLampState()
</script>

<template>
  <section class="tab-panel" aria-label="Home settings">
    <CritterNameplate v-model="settings" id="nameplate" />

    <h1 class="gold">Lamp Brightness</h1>
    <FormField id="brightness">
      <BrightnessSlider
        :model-value="settings.lamp?.brightness || 0"
        @update:model-value="(value) => updateSetting('lamp.brightness', value)"
        id="brightness"
        :min="0"
        :max="100"
        append="%"
        :disabled="disabled || settings.lamp?.homeMode"
      />
    </FormField>

    <h1 class="yellow">Lamp Color Settings</h1>
    <FormField label="Shade" id="shadeColors">
      <ColorGradient
        :model-value="settings.shade?.colors || ['#FF0000FF']"
        @update:model-value="(value) => updateSetting('shade.colors', value)"
        :show-add-button="false"
        :max-colors="1"
        :disabled="disabled"
      />
    </FormField>

    <FormField label="Base" id="baseColors">
      <ColorGradient
        :model-value="settings.base?.colors || ['#FF0000FF']"
        @update:model-value="(value) => updateSetting('base.colors', value)"
        :disabled="disabled"
        :active-color="settings.base?.ac || 0"
        @update:active-color="(value) => updateSetting('base.ac', value)"
      />
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
</style>

