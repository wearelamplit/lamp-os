/**
 * Initialize State Plugin
 *
 * Sets up Pinia stores and initializes application state on startup.
 */

import type { App } from 'vue'
import { createPinia } from 'pinia'

export const pinia = createPinia()

export const InitializeStatePlugin = {
  install(app: App) {
    // Install Pinia
    app.use(pinia)
  },
}

export default InitializeStatePlugin

