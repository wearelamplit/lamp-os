// Copies vite's gzipped bundle into the lamp firmware's SPIFFS data dir, where
// `pio run -t buildfs` packs it into spiffs.bin and the webapp serves it as a
// file. Replaces the old embed-into-a-C-header step.

import { copyFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = dirname(fileURLToPath(import.meta.url))

const src = resolve(__dirname, '..', 'dist', 'index.html.gz')
const dst = resolve(__dirname, '..', '..', 'lamp-os', 'data', 'index.html.gz')

copyFileSync(src, dst)
console.log(`[copy-bundle] ${src} -> ${dst}`)
