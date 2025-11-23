<script setup lang="ts">
import Critter from './Critter-7.vue'
import type { Settings } from '../types'
const model = defineModel<Settings>({ default: { lamp: { name: '' } } })
</script>

<template>
  <div class="nameplate-container">
    <div class="critter">
      <Critter />
    </div>
    <div class="text">
      <div class="preamble">Hello my name is:</div>
      <div class="lampname">{{ model.lamp?.name }}</div>
    </div>
  </div>
</template>

<style scoped>
.nameplate-container {
  display: flex;
  align-items: center;
  margin: 40px 0;
}

.critter {
  width: 51px;
  color: white;
  float: left;
  margin-left: 10px;
}

.nameplate-container .text {
  float: left;
  margin-left: 35px;
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
</style>
