import path from 'path'
import { app } from 'electron'

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
  // Native addons cannot be loaded from inside an asar archive.
  // process.resourcesPath points to the unpacked resources dir outside the asar.
  const addonPath = app.isPackaged
    ? path.join(process.resourcesPath, 'native/build/Release/vcam.node')
    : path.join(__dirname, '../../native/build/Release/vcam.node')
  console.log(`[vcam] loading addon from ${addonPath}`)
  try {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const loaded = require(addonPath) as VCamAddon
    console.log('[vcam] addon loaded ok')
    return loaded
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    console.warn(`[vcam] addon load failed: ${msg}`)
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
