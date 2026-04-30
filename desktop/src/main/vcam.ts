import path from 'path'
import { app } from 'electron'
import fs from 'fs'

interface VCamAddon {
  start(): { ok: boolean; obs: boolean }
  stop(): void
  pushFrame(width: number, height: number, rgba: Buffer): void
}

let addon: VCamAddon | null = null

function loadAddon(): VCamAddon | null {
  if (process.platform === 'darwin') {
    console.log('[vcam] platform=darwin — native addon skipped')
    return null
  }
  const addonPath = app.isPackaged
    ? path.join(process.resourcesPath, 'native/build/Release/vcam.node')
    : path.join(__dirname, '../../native/build/Release/vcam.node')
  console.log(`[vcam] loading addon from ${addonPath}`)
  console.log(`[vcam] process ABI=${process.versions.modules} node=${process.versions.node} electron=${process.versions.electron} execPath=${process.execPath}`)
  const exists = (() => { try { return fs.existsSync(addonPath) } catch { return 'unknown' } })()
  console.log(`[vcam] addon file exists=${exists}`)
  if (!exists) return null
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const loaded = require(addonPath) as VCamAddon
    console.log('[vcam] addon loaded ok')
    return loaded
  } catch (e) {
    const err = e instanceof Error ? e : new Error(String(e))
    console.warn(`[vcam] addon load failed: ${err.message}`)
    if (err.stack) console.warn(`[vcam] addon load stack: ${err.stack}`)
    return null
  }
}

export function startVirtualCamera(): { ok: boolean; obs?: boolean; error?: string } {
  if (process.platform === 'darwin') {
    console.log('[vcam] startVirtualCamera — skipped on darwin')
    return { ok: false, error: 'macOS virtual camera not supported' }
  }
  console.log('[vcam] startVirtualCamera — loading addon')
  addon = loadAddon()
  if (!addon) {
    console.warn('[vcam] startVirtualCamera — addon null, cannot start')
    return { ok: false, error: 'Native addon failed to load' }
  }
  console.log('[vcam] startVirtualCamera — calling addon.start()')
  let result: { ok: boolean; obs: boolean }
  try {
    result = addon.start()
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    console.error(`[vcam] addon.start() threw: ${msg}`)
    return { ok: false, error: msg }
  }
  console.log(`[vcam] addon.start() returned ok=${result.ok} obs=${result.obs}`)
  return result.ok ? { ok: true, obs: result.obs } : { ok: false, error: 'Failed to start virtual camera device' }
}

export function stopVirtualCamera() {
  if (!addon) {
    console.log('[vcam] stopVirtualCamera — addon already null, nothing to stop')
    return
  }
  console.log('[vcam] stopVirtualCamera — calling addon.stop()')
  try {
    addon.stop()
    console.log('[vcam] addon.stop() done')
  } catch (e) {
    console.warn(`[vcam] addon.stop() threw: ${e instanceof Error ? e.message : String(e)}`)
  }
  addon = null
}

export function pushFrame(width: number, height: number, rgba: Buffer) {
  if (!addon) return
  addon.pushFrame(width, height, rgba)
}
