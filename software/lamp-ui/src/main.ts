import './assets/main.css'

import { createApp } from 'vue'
import { createRouter, createWebHistory } from 'vue-router'

import App from './App.vue'
import FieldsPlugin from './plugins/fields'

// Import layouts
import LampLayout from './layouts/LampLayout.vue'

// Import pages
import FormDemoPage from './pages/FormDemoPage.vue'

// Lamp pages (lazy loaded)
const LampHomePage = () => import('./pages/lamp/LampHomePage.vue')
const ExpressionsPage = () => import('./pages/lamp/ExpressionsPage.vue')
const SetupPage = () => import('./pages/lamp/SetupPage.vue')
const InfoPage = () => import('./pages/lamp/InfoPage.vue')

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
          name: 'lamp-home',
          component: LampHomePage,
        },
        {
          path: 'expressions',
          name: 'lamp-expressions',
          component: ExpressionsPage,
        },
        {
          path: 'setup',
          name: 'lamp-setup',
          component: SetupPage,
        },
        {
          path: 'info',
          name: 'lamp-info',
          component: InfoPage,
        },
      ],
    },
    {
      path: '/form',
      name: 'form-demo',
      component: FormDemoPage,
    },
  ],
})

const app = createApp(App)

// Register plugins
app.use(router)
app.use(FieldsPlugin)

app.mount('#app')
