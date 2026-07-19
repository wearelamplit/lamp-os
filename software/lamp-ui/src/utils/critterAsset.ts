// Must match the Flutter app's critter recolor so the same lamp shows the same tint everywhere.
const SHADE_GRADIENT_RE = /<linearGradient[^>]*id="[^"]*Shade"[^>]*>[\s\S]*?<\/linearGradient>/
const BODY_GRADIENT_RE = /<linearGradient[^>]*id="[^"]*Body"[^>]*>[\s\S]*?<\/linearGradient>/

const stopTag = (pct: number, color: string) =>
  `<stop offset="${pct}%" style="stop-color:${color};stop-opacity:1"/>`

const rewriteGradient = (tag: string, stops: string) =>
  `${tag.slice(0, tag.indexOf('>') + 1)}${stops}</linearGradient>`

export function recolorCritterSvg(template: string, shadeColor: string, bodyColor: string): string {
  const shadeStops = stopTag(0, shadeColor) + stopTag(100, shadeColor)
  const bodyStops = stopTag(0, bodyColor) + stopTag(100, bodyColor)
  return template
    .replace(SHADE_GRADIENT_RE, (m) => rewriteGradient(m, shadeStops))
    .replace(BODY_GRADIENT_RE, (m) => rewriteGradient(m, bodyStops))
}

export function critterAssetPath(key: number | 'stray'): string {
  return `/critter-${key}.svg`
}

// Caches the in-flight promise so two consumers (nameplate + favicon)
// requesting the same critter before the first fetch resolves share one
// network request.
const templateCache = new Map<string, Promise<string>>()

export function fetchCritterTemplate(path: string): Promise<string> {
  const cached = templateCache.get(path)
  if (cached) return cached
  const promise = fetch(path).then((res) => {
    if (!res.ok) throw new Error(`critter fetch failed: ${path}`)
    return res.text()
  })
  promise.catch(() => templateCache.delete(path))
  templateCache.set(path, promise)
  return promise
}
