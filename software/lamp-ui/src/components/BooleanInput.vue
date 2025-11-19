<script setup lang="ts">
interface Props {
  modelValue: boolean
  disabled?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  disabled: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: boolean]
}>()

const toggleValue = () => {
  if (!props.disabled) {
    emit('update:modelValue', !props.modelValue)
  }
}
</script>

<template>
  <div class="boolean-input-container">
    <button
      type="button"
      class="boolean-input"
      :class="{
        'boolean-input--active': modelValue,
        'boolean-input--disabled': disabled,
      }"
      @click="toggleValue"
      :disabled="disabled"
      :aria-checked="modelValue"
      role="switch"
      :aria-label="modelValue ? 'Enabled' : 'Disabled'"
    >
      <div class="boolean-input-track">
        <div class="boolean-input-thumb"></div>
      </div>
    </button>
  </div>
</template>

<style scoped>
.boolean-input-container {
  display: flex;
  align-items: center;
  width: 100%;
}

.boolean-input {
  position: relative;
  width: 52px;
  height: 28px;
  border: none;
  border-radius: 14px;
  background-color: var(--color-background-mute);
  cursor: pointer;
  transition: all 0.2s ease;
  padding: 0;
  display: flex;
  align-items: center;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
}

.boolean-input:focus {
  outline: none;
  box-shadow: 0 0 0 3px rgba(68, 108, 156, 0.2);
}

.boolean-input--active {
  background-color: var(--color-background-mute);
}

.boolean-input--disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.boolean-input-track {
  position: relative;
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
}

.boolean-input-thumb {
  position: absolute;
  left: 2px;
  width: 24px;
  height: 24px;
  background-color: var(--brand-slate-grey);
  border-radius: 50%;
  transition: all 0.2s ease;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.3);
}

.boolean-input--active .boolean-input-thumb {
  left: calc(100% - 26px);
  background-color: var(--brand-lumen-green);
}
</style>
