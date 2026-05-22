import { fileURLToPath, URL } from 'node:url'
import { readFileSync, writeFileSync, existsSync, unlinkSync } from 'fs'
import { join } from 'path'

import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import vueDevTools from 'vite-plugin-vue-devtools'
import compression from 'vite-plugin-compression'

// Custom plugin to inline CSS and JS into HTML
const inlineAssetsPlugin = () => ({
  name: 'inline-assets',
  writeBundle(options: { dir?: string }) {
    const distPath = options.dir || 'dist'
    const htmlPath = join(distPath, 'index.html')
    const cssPath = join(distPath, 'index.css')
    const jsPath = join(distPath, 'index.js')

    try {
      let html = readFileSync(htmlPath, 'utf-8')

      // Inline CSS
      if (existsSync(cssPath)) {
        const css = readFileSync(cssPath, 'utf-8')
        html = html.replace(/<link rel="stylesheet" href="\/index\.css">/, `<style>${css}</style>`)
        // Remove the CSS file
        unlinkSync(cssPath)
      }

      // Inline JS. Escape </script> inside the bundle — vue-router (and other
      // libs) ship strings like "<script ...></script>" in their code, and
      // when those are embedded raw into an inline <script>, the HTML parser
      // sees the embedded </script> and closes the script tag mid-bundle.
      // <\/script> is equivalent to </script> in a JS string but does not
      // terminate an HTML script element.
      if (existsSync(jsPath)) {
        const js = readFileSync(jsPath, 'utf-8').replace(/<\/script>/g, '<\\/script>')
        html = html.replace(
          /<script type="module" crossorigin src="\/index\.js"><\/script>/,
          `<script type="module">${js}</script>`,
        )
        // Remove the JS file
        unlinkSync(jsPath)
      }

      // Write the updated HTML
      writeFileSync(htmlPath, html)
    } catch (error) {
      console.warn('Failed to inline assets:', error)
    }
  },
})

// https://vite.dev/config/
export default defineConfig(({ command }) => ({
  plugins: [
    vue(),
    // Only include dev tools in development
    command === 'serve' && vueDevTools(),
    // Add compression for production builds
    command === 'build' &&
    compression({
      algorithm: 'gzip',
      ext: '.gz',
      threshold: 0,
      deleteOriginFile: false,
    }),
    // Custom plugin to inline assets
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
    // Enable minification
    minify: 'terser',
    // Configure terser options for better minification
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true,
        pure_funcs: ['console.log', 'console.info', 'console.debug', 'console.warn'],
        // Additional compression options
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
    // Enable source maps for debugging (optional - can be disabled for smaller builds)
    sourcemap: false,
    // Configure rollup to create a single bundle
    rollupOptions: {
      output: {
        // Create a single file bundle
        manualChunks: undefined,
        // Inline all assets into the HTML
        inlineDynamicImports: true,
        // Single file output
        entryFileNames: 'index.js',
        chunkFileNames: 'index.js',
        assetFileNames: 'index.[ext]',
      },
    },
    // Disable CSS code splitting to inline CSS
    cssCodeSplit: false,
    // Target modern browsers for smaller bundles
    target: 'es2015',
    // Increase chunk size warning limit since we're creating a single bundle
    chunkSizeWarningLimit: 5000,
    // Additional build optimizations
    reportCompressedSize: true,
    emptyOutDir: true,
    // Configure assets to be inlined
    assetsInlineLimit: Infinity, // Inline all assets regardless of size
  },
  // Optimize dependencies
  optimizeDeps: {
    include: ['vue', 'pinia'],
  },
}))
