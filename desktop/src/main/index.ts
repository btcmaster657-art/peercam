import { app, BrowserWindow, ipcMain, session } from 'electron'
import path from 'path'
import fs from 'fs'
import { startVirtualCamera, stopVirtualCamera, pushFrame } from './vcam'

const isDev = !app.isPackaged

// ── File logger ───────────────────────────────────────────────────────────────
const LOG_PATH     = path.join(app.getPath('userData'), 'peercam.log')
const MAX_LOG_BYTES = 2 * 1024 * 1024

function rotateLogs() {
  try {
    const stat = fs.statSync(LOG_PATH)
    if (stat.size > MAX_LOG_BYTES) {
      fs.renameSync(LOG_PATH, LOG_PATH + '.old')
      writeLog('INFO', 'log rotated — previous log saved to peercam.log.old')
    }
  } catch { /* file doesn't exist yet */ }
}

function writeLog(level: 'INFO' | 'WARN' | 'ERROR', ...args: unknown[]) {
  const line = `[${new Date().toISOString()}] [${level}] ${args.map(a =>
    typeof a === 'string' ? a : JSON.stringify(a)
  ).join(' ')}\n`
  try { fs.appendFileSync(LOG_PATH, line) } catch { /* disk full etc */ }
}

rotateLogs()
writeLog('INFO', `PeerCam starting v${app.getVersion()} pid=${process.pid} platform=${process.platform} arch=${process.arch} electron=${process.versions.electron} node=${process.versions.node}`)
writeLog('INFO', `log file: ${LOG_PATH}`)

const _consoleLog   = console.log.bind(console)
const _consoleWarn  = console.warn.bind(console)
const _consoleError = console.error.bind(console)
console.log   = (...a) => { _consoleLog(...a);   writeLog('INFO',  ...a) }
console.warn  = (...a) => { _consoleWarn(...a);  writeLog('WARN',  ...a) }
console.error = (...a) => { _consoleError(...a); writeLog('ERROR', ...a) }

// ── Process-level error traps ─────────────────────────────────────────────────
process.on('uncaughtException', (err) => {
  writeLog('ERROR', `[main] uncaughtException: ${err.message}\n${err.stack ?? ''}`)
})
process.on('unhandledRejection', (reason) => {
  writeLog('ERROR', `[main] unhandledRejection: ${reason instanceof Error ? reason.stack ?? reason.message : String(reason)}`)
})

// ── Window ────────────────────────────────────────────────────────────────────
function createWindow() {
  writeLog('INFO', '[main] creating BrowserWindow')
  const win = new BrowserWindow({
    width: 480,
    height: 680,
    resizable: false,
    title: 'PeerCam',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  })

  if (isDev) {
    writeLog('INFO', '[main] loading dev URL http://localhost:5173')
    win.loadURL('http://localhost:5173')
  } else {
    const htmlPath = path.join(__dirname, '../renderer/index.html')
    writeLog('INFO', `[main] loading file ${htmlPath}`)
    win.loadFile(htmlPath)
  }

  win.webContents.on('did-finish-load', () =>
    writeLog('INFO', '[main] renderer did-finish-load'))

  win.webContents.on('did-fail-load', (_e, code, desc, url) =>
    writeLog('ERROR', `[main] renderer did-fail-load code=${code} desc="${desc}" url=${url}`))

  win.webContents.on('render-process-gone', (_e, details) =>
    writeLog('ERROR', `[main] renderer process gone — reason=${details.reason} exitCode=${details.exitCode}`))

  win.webContents.on('unresponsive', () =>
    writeLog('WARN', '[main] renderer became unresponsive'))

  win.webContents.on('responsive', () =>
    writeLog('INFO', '[main] renderer became responsive again'))

  win.webContents.on('console-message', (_e, level, message, line, sourceId) => {
    const lvl = level === 3 ? 'ERROR' : level === 2 ? 'WARN' : 'INFO'
    writeLog(lvl, '[renderer]', message, sourceId ? `(${sourceId}:${line})` : '')
  })

  win.on('close', () => writeLog('INFO', '[main] window close event'))
  win.on('closed', () => writeLog('INFO', '[main] window closed'))
}

app.whenReady().then(() => {
  writeLog('INFO', `[main] app ready — userData=${app.getPath('userData')}`)
  session.defaultSession.setPermissionRequestHandler((_wc, permission, cb) => {
    const granted = permission === 'media'
    writeLog('INFO', `[main] permission request permission=${permission} granted=${granted}`)
    cb(granted)
  })
  createWindow()
  app.on('activate', () => {
    writeLog('INFO', '[main] app activate')
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  writeLog('INFO', '[main] all windows closed — shutting down')
  stopVirtualCamera()
  if (process.platform !== 'darwin') app.quit()
})

app.on('before-quit', () => writeLog('INFO', '[main] before-quit'))
app.on('quit', (_e, code) => writeLog('INFO', `[main] quit exitCode=${code}`))

// ── IPC handlers ──────────────────────────────────────────────────────────────

ipcMain.handle('vcam:start', async () => {
  writeLog('INFO', '[ipc] vcam:start called')
  try {
    const result = startVirtualCamera()
    writeLog(result.ok ? 'INFO' : 'WARN', `[ipc] vcam:start result ok=${result.ok} obs=${result.obs ?? false}${result.error ? ` error="${result.error}"` : ''}`)
    return result
  } catch (e: unknown) {
    const msg = e instanceof Error ? e.message : String(e)
    writeLog('ERROR', `[ipc] vcam:start threw: ${msg}`)
    return { ok: false, obs: false, error: msg }
  }
})

ipcMain.handle('vcam:stop', async () => {
  writeLog('INFO', '[ipc] vcam:stop called')
  try {
    stopVirtualCamera()
    writeLog('INFO', '[ipc] vcam:stop done')
  } catch (e: unknown) {
    writeLog('WARN', `[ipc] vcam:stop threw: ${e instanceof Error ? e.message : String(e)}`)
  }
})

ipcMain.handle('vcam:pushFrame', (_e, width: number, height: number, rgba: Uint8Array) => {
  try {
    pushFrame(width, height, Buffer.from(rgba.buffer, rgba.byteOffset, rgba.byteLength))
  } catch (e: unknown) {
    writeLog('WARN', `[ipc] vcam:pushFrame threw ${width}x${height}: ${e instanceof Error ? e.message : String(e)}`)
  }
})

ipcMain.handle('log', (_e, level: string, message: string) => {
  writeLog(level as 'INFO' | 'WARN' | 'ERROR', '[renderer]', message)
})

ipcMain.handle('getLogPath', () => {
  writeLog('INFO', '[ipc] getLogPath called')
  return LOG_PATH
})
