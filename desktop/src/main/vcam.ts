import path from 'path'

// Native addon interface
interface VCamAddon {
  start(): boolean
  stop(): void
  pushFrame(width: number, height: number, rgba: Buffer): void
}

let addon: VCamAddon | null = null

function loadAddon(): VCamAddon | null {
  if (process.platform === 'darwin') return null // macOS stubbed
  try {
    const addonPath = path.join(__dirname, '../../native/build/Release/vcam.node')
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    return require(addonPath) as VCamAddon
  } catch (e) {
    console.warn('[vcam] Native addon not found — frame push disabled:', e)
    return null
  }
}

export function startVirtualCamera(): { ok: boolean; error?: string } {
  if (process.platform === 'darwin') {
    return { ok: false, error: 'macOS virtual camera not supported' }
  }
  addon = loadAddon()
  if (!addon) return { ok: false, error: 'Native addon failed to load' }
  const ok = addon.start()
  return ok ? { ok: true } : { ok: false, error: 'Failed to start virtual camera device' }
}

export function stopVirtualCamera() {
  addon?.stop()
  addon = null
}

export function pushFrame(width: number, height: number, rgba: Buffer) {
  addon?.pushFrame(width, height, rgba)
}
