import { fileURLToPath, URL } from 'node:url'
import { readFileSync, writeFileSync, existsSync, unlinkSync } from 'fs'
import { join } from 'path'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import compression from 'vite-plugin-compression'

// Read the firmware version from the root VERSION file (the single source both
// firmwares derive from) so the UI shows the build it's paired with. Falls back
// to 'dev' for local runs without the firmware tree.
function readFwVersion(): string {
  try {
    return readFileSync(join(__dirname, '..', '..', 'VERSION'), 'utf-8').trim() || 'dev'
  } catch {
    return 'dev'
  }
}

// Inline emitted CSS / JS into the single index.html so the firmware can
// serve one gzipped blob with no further asset resolution.
const inlineAssetsPlugin = () => ({
  name: 'inline-assets',
  writeBundle(options: { dir?: string }) {
    const distPath = options.dir || 'dist'
    const htmlPath = join(distPath, 'index.html')
    const cssPath = join(distPath, 'index.css')
    const jsPath = join(distPath, 'index.js')

    try {
      let html = readFileSync(htmlPath, 'utf-8')

      if (existsSync(cssPath)) {
        const css = readFileSync(cssPath, 'utf-8')
        html = html.replace(/<link rel="stylesheet" href="\/index\.css">/, `<style>${css}</style>`)
        unlinkSync(cssPath)
      }

      if (existsSync(jsPath)) {
        const js = readFileSync(jsPath, 'utf-8')
        html = html.replace(
          /<script type="module" crossorigin src="\/index\.js"><\/script>/,
          `<script type="module">${js}</script>`,
        )
        unlinkSync(jsPath)
      }

      writeFileSync(htmlPath, html)
    } catch (error) {
      console.warn('Failed to inline assets:', error)
    }
  },
})

export default defineConfig(({ command }) => ({
  define: {
    'import.meta.env.VITE_FW_VERSION': JSON.stringify(readFwVersion()),
  },
  plugins: [
    vue(),
    command === 'build' &&
      compression({
        algorithm: 'gzip',
        ext: '.gz',
        threshold: 0,
        deleteOriginFile: false,
      }),
    command === 'build' && inlineAssetsPlugin(),
  ].filter(Boolean),
  resolve: {
    alias: {
      '@': fileURLToPath(new URL('./src', import.meta.url)),
    },
  },
  server: {
    open: true,
  },
  build: {
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true,
        pure_funcs: ['console.log', 'console.info', 'console.debug', 'console.warn'],
        passes: 2,
        unsafe: true,
        unsafe_comps: true,
        unsafe_Function: true,
        unsafe_math: true,
        unsafe_proto: true,
        unsafe_regexp: true,
        unsafe_undefined: true,
      },
      format: {
        comments: false,
      },
      mangle: {
        safari10: true,
      },
    },
    sourcemap: false,
    rollupOptions: {
      output: {
        manualChunks: undefined,
        inlineDynamicImports: true,
        entryFileNames: 'index.js',
        chunkFileNames: 'index.js',
        assetFileNames: 'index.[ext]',
      },
    },
    cssCodeSplit: false,
    target: 'es2015',
    chunkSizeWarningLimit: 5000,
    reportCompressedSize: true,
    emptyOutDir: true,
    assetsInlineLimit: Infinity,
  },
  optimizeDeps: {
    include: ['vue'],
  },
}))
