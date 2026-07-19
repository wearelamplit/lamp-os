<script setup lang="ts">
import { computed } from 'vue'
import CritterIcon from './CritterIcon.vue'
import { SAFE_COLOR } from '../utils/configShape'

const props = withDefaults(
  defineProps<{ name?: string; lampId?: string; baseColor?: string; shadeColor?: string; size?: number }>(),
  { size: 51 },
)

const isUnnamed = computed(() => !props.name)
</script>

<template>
  <div class="nameplate-container">
    <div class="critter" :style="{ width: size + 'px' }">
      <CritterIcon
        :lamp-id="lampId"
        :shade="shadeColor ?? SAFE_COLOR"
        :base="baseColor ?? SAFE_COLOR"
        :stray="isUnnamed"
      />
    </div>
    <div class="text">
      <div class="preamble">Hello my name is:</div>
      <div class="lampname" :class="{ unnamed: isUnnamed }">{{ isUnnamed ? 'a nameless friend' : name }}</div>
    </div>
  </div>
</template>

<style scoped>
.nameplate-container {
  display: flex;
  align-items: center;
  gap: 35px;
  margin: 40px 0;
}

.critter {
  margin-left: 10px;
}

.nameplate-container .text .preamble {
  font-size: 13px;
  font-weight: 100;
  color: #b1aa92;
}

.nameplate-container .text .lampname {
  font-size: 28px;
  font-weight: 800;
  color: #ffffff;
  margin-top: -9px;
}

.nameplate-container .text .lampname.unnamed {
  font-size: 20px;
  font-weight: 400;
  font-style: italic;
  color: #b1aa92;
}
</style>
