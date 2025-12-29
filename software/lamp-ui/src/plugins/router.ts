/**
 * Router Plugin
 *
 * Configures Vue Router with all application routes.
 */

import { createRouter, createWebHistory } from 'vue-router'

// Import layouts
import LampLayout from '@/layouts/LampLayout.vue'

// Pages (lazy loaded)
const IndexPage = () => import('@/pages/Index.vue')
const ExpressionsPage = () => import('@/pages/Expressions.vue')
const SetupPage = () => import('@/pages/Setup.vue')
const InfoPage = () => import('@/pages/Info.vue')
const FormPage = () => import('@/pages/Form.vue')

// Create and export router instance
export const router = createRouter({
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

export default router

