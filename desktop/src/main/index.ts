import { app, BrowserWindow, ipcMain, session } from 'electron'
import path from 'path'
import fs from 'fs'
import { startVirtualCamera, stopVirtualCamera, pushFrame } from './vcam'

const isDev = !app.isPackaged

// ── File logger ───────────────────────────────────────────────────────────────
const LOG_PATH = path.join(app.getPath('userData'), 'peercam.log')
const MAX_LOG_BYTES = 2 * 1024 * 1024 // 2 MB — rotate when exceeded

function rotateLogs() {
  try {
    const stat = fs.statSync(LOG_PATH)
    if (stat.size > MAX_LOG_BYTES) {
      fs.renameSync(LOG_PATH, LOG_PATH + '.old')
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
writeLog('INFO', 'PeerCam starting', app.getVersion())

// Intercept console so renderer logs also go to file via IPC
const _consoleLog   = console.log.bind(console)
const _consoleWarn  = console.warn.bind(console)
const _consoleError = console.error.bind(console)
console.log   = (...a) => { _consoleLog(...a);   writeLog('INFO',  ...a) }
console.warn  = (...a) => { _consoleWarn(...a);  writeLog('WARN',  ...a) }
console.error = (...a) => { _consoleError(...a); writeLog('ERROR', ...a) }

// ── Window ────────────────────────────────────────────────────────────────────
function createWindow() {
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
    win.loadURL('http://localhost:5173')
  } else {
    win.loadFile(path.join(__dirname, '../renderer/index.html'))
  }

  // Forward renderer console to log file
  win.webContents.on('console-message', (_e, level, message) => {
    const lvl = level === 3 ? 'ERROR' : level === 2 ? 'WARN' : 'INFO'
    writeLog(lvl, '[renderer]', message)
  })
}

app.whenReady().then(() => {
  session.defaultSession.setPermissionRequestHandler((_wc, permission, cb) => {
    cb(permission === 'media')
  })
  createWindow()
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  writeLog('INFO', 'PeerCam shutting down')
  stopVirtualCamera()
  if (process.platform !== 'darwin') app.quit()
})

// ── IPC handlers ──────────────────────────────────────────────────────────────

ipcMain.handle('vcam:start', async () => {
  return startVirtualCamera()
})

ipcMain.handle('vcam:stop', async () => {
  stopVirtualCamera()
})

ipcMain.handle('vcam:pushFrame', (_e, width: number, height: number, rgba: Buffer) => {
  pushFrame(width, height, rgba)
})

// Renderer can log directly to file
ipcMain.handle('log', (_e, level: string, message: string) => {
  writeLog(level as 'INFO' | 'WARN' | 'ERROR', '[renderer]', message)
})

// Renderer can get the log file path to show in UI
ipcMain.handle('getLogPath', () => LOG_PATH)
