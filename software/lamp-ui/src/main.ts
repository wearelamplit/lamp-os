import './assets/main.css'

import { createApp } from 'vue'

import App from './App.vue'
import router from './plugins/router'
import GlobalComponentsPlugin from './plugins/globalComponents'
import InitializeStatePlugin from './plugins/initializeState'

const app = createApp(App)

// Register plugins
app.use(InitializeStatePlugin)
app.use(router)
app.use(GlobalComponentsPlugin)

app.mount('#app')
