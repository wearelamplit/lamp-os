// Reads dist/index.html.gz and emits a PROGMEM byte array consumed by
// software/lamp-os/src/components/webapp/webapp.cpp.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = dirname(__filename)

const gzPath = resolve(__dirname, '..', 'dist', 'index.html.gz')
const outPath = resolve(
  __dirname,
  '..',
  '..',
  'lamp-os',
  'src',
  'components',
  'webapp',
  'index_html_gz.h',
)

const bytes = new Uint8Array(readFileSync(gzPath))
const hex = Array.from(bytes, (b) => `0x${b.toString(16).padStart(2, '0')}`)

// 12 bytes per line keeps lines under ~80 cols.
const perLine = 12
const lines = []
for (let i = 0; i < hex.length; i += perLine) {
  lines.push('    ' + hex.slice(i, i + perLine).join(', ') + ',')
}

const header = `#pragma once
#include <Arduino.h>
#include <cstddef>

namespace webapp {
inline constexpr bool kIndexHtmlGzipped = true;
inline constexpr unsigned char kIndexHtml[] = {
${lines.join('\n')}
};
inline constexpr size_t kIndexHtmlLen = sizeof(kIndexHtml);
}  // namespace webapp
`

mkdirSync(dirname(outPath), { recursive: true })
writeFileSync(outPath, header)

console.log(`[embed-bundle] wrote ${bytes.length} bytes -> ${outPath}`)
