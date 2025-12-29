import './assets/main.css'

import { createApp } from 'vue'
import { createRouter, createWebHistory } from 'vue-router'

import App from './App.vue'
import FieldsPlugin from './plugins/fields'

// Import layouts
import LampLayout from './layouts/LampLayout.vue'

// Pages (lazy loaded)
const IndexPage = () => import('./pages/Index.vue')
const ExpressionsPage = () => import('./pages/Expressions.vue')
const SetupPage = () => import('./pages/Setup.vue')
const InfoPage = () => import('./pages/Info.vue')
const FormPage = () => import('./pages/Form.vue')

// Create router
const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/',
      component: LampLayout,
      children: [
        {
          path: '',
          name: 'home',
          component: IndexPage,
        },
        {
          path: 'expressions',
          name: 'expressions',
          component: ExpressionsPage,
        },
        {
          path: 'setup',
          name: 'setup',
          component: SetupPage,
        },
        {
          path: 'info',
          name: 'info',
          component: InfoPage,
        },
      ],
    },
    {
      path: '/form',
      name: 'form',
      component: FormPage,
    },
  ],
})

const app = createApp(App)

// Register plugins
app.use(router)
app.use(FieldsPlugin)

app.mount('#app')
