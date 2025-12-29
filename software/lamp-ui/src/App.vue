<script setup lang="ts">
// App.vue is now just a router outlet
</script>

<template>
  <router-view />
</template>

<style>
#app {
  min-height: 100vh;
  width: 100%;
}

/* Prevent zooming on mobile devices for all interactive elements */
button,
input,
select,
textarea,
a,
[role='button'],
[tabindex] {
  touch-action: manipulation;
  -webkit-touch-callout: none;
  -webkit-user-select: none;
  -khtml-user-select: none;
  -moz-user-select: none;
  -ms-user-select: none;
  user-select: none;
}

/* Allow text selection in input fields and textareas */
input[type='text'],
input[type='email'],
input[type='password'],
input[type='search'],
input[type='url'],
input[type='tel'],
textarea {
  -webkit-user-select: text;
  -khtml-user-select: text;
  -moz-user-select: text;
  -ms-user-select: text;
  user-select: text;
}
</style>
