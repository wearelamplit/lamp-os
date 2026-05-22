<template>
  <button
    type="button"
    class="icon-button"
    :class="buttonClass"
    :disabled="disabled"
    @click="$emit('click')"
    :title="title"
    :aria-label="title || icon"
  >
    <slot>
      <svg
        width="16"
        height="16"
        viewBox="0 0 20 20"
        fill="none"
        xmlns="http://www.w3.org/2000/svg"
      >
        <path
          v-if="icon === 'star'"
          d="M9.99999 1.66666L12.575 6.88332L18.3333 7.72499L14.1667 11.7833L15.15 17.5167L9.99999 14.8083L4.84999 17.5167L5.83332 11.7833L1.66666 7.72499L7.42499 6.88332L9.99999 1.66666Z"
          stroke="currentColor"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
        />
        <g v-else-if="icon === 'clone'">
          <rect
            x="7"
            y="7"
            width="11"
            height="11"
            rx="1.5"
            stroke="currentColor"
            stroke-width="2"
            fill="none"
          />
          <path
            d="M4.5 13V4.5C4.5 3.39543 5.39543 2.5 6.5 2.5H13"
            stroke="currentColor"
            stroke-width="2"
            stroke-linecap="round"
            fill="none"
          />
        </g>
        <path
          v-else-if="icon === 'remove'"
          d="M2.5 4.99999H4.16667M4.16667 4.99999H17.5M4.16667 4.99999L4.16667 16.6667C4.16667 17.1087 4.34226 17.5326 4.65482 17.8452C4.96738 18.1577 5.39131 18.3333 5.83333 18.3333H14.1667C14.6087 18.3333 15.0326 18.1577 15.3452 17.8452C15.6577 17.5326 15.8333 17.1087 15.8333 16.6667V4.99999M6.66667 4.99999V3.33332C6.66667 2.8913 6.84226 2.46737 7.15482 2.15481C7.46738 1.84225 7.89131 1.66666 8.33333 1.66666H11.6667C12.1087 1.66666 12.5326 1.84225 12.8452 2.15481C13.1577 2.46737 13.3333 2.8913 13.3333 3.33332V4.99999"
          stroke="currentColor"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
        />
        <path
          v-if="icon === 'plus'"
          d="M10 3.33332V16.6667M3.33334 9.99999H16.6667"
          stroke="currentColor"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
        />
        <path
          v-else-if="icon === 'minus'"
          d="M3.33334 9.99999H16.6667"
          stroke="currentColor"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
        />
      </svg>
    </slot>
  </button>
</template>

<script setup lang="ts">
import { computed } from 'vue'

interface Props {
  icon?: 'star' | 'clone' | 'remove' | 'plus' | 'minus'
  title?: string
  variant?: 'star' | 'clone' | 'remove' | 'plus' | 'minus'
  disabled?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  icon: 'star',
  title: '',
  variant: 'star',
  disabled: false,
})

defineEmits<{
  click: []
}>()

const buttonClass = computed(() => {
  return `${props.variant}-button`
})
</script>

<style scoped>
.icon-button {
  width: 40px;
  height: 40px;
  border: none;
  background: var(--color-background-soft);
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.2s ease;
  color: var(--brand-fog-grey);
  flex-shrink: 0;
  touch-action: manipulation;
  -webkit-touch-callout: none;
  -webkit-user-select: none;
  -khtml-user-select: none;
  -moz-user-select: none;
  -ms-user-select: none;
  user-select: none;
}

.icon-button:hover {
  background: var(--color-background-mute);
  transform: scale(1.05);
}

.icon-button:active {
  transform: scale(0.95);
}

.star-button:hover {
  color: var(--brand-amber-gold);
  border-color: var(--brand-amber-gold);
}

.clone-button:hover {
  color: var(--brand-aurora-blue);
  border-color: var(--brand-aurora-blue);
}

.remove-button:hover {
  color: var(--color-error);
  border-color: var(--color-error);
}

.plus-button:hover {
  color: var(--brand-lumen-green);
  border-color: var(--brand-lumen-green);
}

.minus-button:hover {
  color: var(--brand-amber-gold);
  border-color: var(--brand-amber-gold);
}

/* Disabled state */
.icon-button:disabled {
  opacity: 0.5;
  cursor: not-allowed;
  pointer-events: none;
}
</style>

