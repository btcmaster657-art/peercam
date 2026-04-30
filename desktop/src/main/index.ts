// import { app, BrowserWindow, ipcMain, Menu, Tray, nativeImage, session } from 'electron'
// import path from 'path'
// import fs from 'fs'
// import { startVirtualCamera, stopVirtualCamera, pushFrame } from './vcam'

// const isDev = !app.isPackaged

// // ── File logger ───────────────────────────────────────────────────────────────
// const LOG_PATH     = path.join(app.getPath('userData'), 'peercam.log')
// const MAX_LOG_BYTES = 2 * 1024 * 1024

// function rotateLogs() {
//   try {
//     const stat = fs.statSync(LOG_PATH)
//     if (stat.size > MAX_LOG_BYTES) {
//       fs.renameSync(LOG_PATH, LOG_PATH + '.old')
//       writeLog('INFO', 'log rotated — previous log saved to peercam.log.old')
//     }
//   } catch { /* file doesn't exist yet */ }
// }

// function writeLog(level: 'INFO' | 'WARN' | 'ERROR', ...args: unknown[]) {
//   const line = `[${new Date().toISOString()}] [${level}] ${args.map(a =>
//     typeof a === 'string' ? a : JSON.stringify(a)
//   ).join(' ')}\n`
//   try { fs.appendFileSync(LOG_PATH, line) } catch { /* disk full etc */ }
// }

// rotateLogs()
// writeLog('INFO', `PeerCam starting v${app.getVersion()} pid=${process.pid} platform=${process.platform} arch=${process.arch} electron=${process.versions.electron} node=${process.versions.node} modules_abi=${process.versions.modules}`)
// writeLog('INFO', `log file: ${LOG_PATH}`)
// writeLog('INFO', `resourcesPath=${process.resourcesPath ?? 'n/a'} isPackaged=${app.isPackaged}`)

// const _consoleLog   = console.log.bind(console)
// const _consoleWarn  = console.warn.bind(console)
// const _consoleError = console.error.bind(console)
// console.log   = (...a) => { _consoleLog(...a);   writeLog('INFO',  ...a) }
// console.warn  = (...a) => { _consoleWarn(...a);  writeLog('WARN',  ...a) }
// console.error = (...a) => { _consoleError(...a); writeLog('ERROR', ...a) }

// // ── Process-level error traps ─────────────────────────────────────────────────
// process.on('uncaughtException', (err, origin) => {
//   writeLog('ERROR', `[main] uncaughtException origin=${origin} message="${err.message}"\n${err.stack ?? ''}`)
// })
// process.on('unhandledRejection', (reason, promise) => {
//   void promise
//   writeLog('ERROR', `[main] unhandledRejection: ${reason instanceof Error ? reason.stack ?? reason.message : String(reason)}`)
// })
// process.on('SIGTERM', () => writeLog('WARN', '[main] SIGTERM received'))
// process.on('SIGINT',  () => writeLog('WARN', '[main] SIGINT received'))

// // ── Window ────────────────────────────────────────────────────────────────────
// function createWindow() {
//   writeLog('INFO', '[main] creating BrowserWindow')
//   const win = new BrowserWindow({
//     width: 480,
//     height: 680,
//     resizable: false,
//     title: 'PeerCam',
//     webPreferences: {
//       preload: path.join(__dirname, 'preload.js'),
//       contextIsolation: true,
//       nodeIntegration: false,
//     },
//   })

//   if (isDev) {
//     writeLog('INFO', '[main] loading dev URL http://localhost:5173')
//     win.loadURL('http://localhost:5173')
//   } else {
//     const htmlPath = path.join(__dirname, '../renderer/index.html')
//     writeLog('INFO', `[main] loading file ${htmlPath}`)
//     win.loadFile(htmlPath)
//   }

//   win.webContents.on('did-finish-load', () =>
//     writeLog('INFO', '[main] renderer did-finish-load'))

//   win.webContents.on('did-fail-load', (_e, code, desc, url) =>
//     writeLog('ERROR', `[main] renderer did-fail-load code=${code} desc="${desc}" url=${url}`))

//   win.webContents.on('render-process-gone', (_e, details) => {
//     writeLog('ERROR', `[main] renderer process gone — reason=${details.reason} exitCode=${details.exitCode}`)
//     writeLog('ERROR', `[main] renderer crash details: ${JSON.stringify(details)}`)
//   })

//   win.webContents.on('unresponsive', () =>
//     writeLog('WARN', '[main] renderer became unresponsive'))

//   win.webContents.on('responsive', () =>
//     writeLog('INFO', '[main] renderer became responsive again'))

//   win.webContents.on('console-message', (_e, level, message, line, sourceId) => {
//     const lvl = level === 3 ? 'ERROR' : level === 2 ? 'WARN' : 'INFO'
//     writeLog(lvl, '[renderer]', message, sourceId ? `(${sourceId}:${line})` : '')
//   })

//   win.on('close', () => writeLog('INFO', '[main] window close event'))
//   win.on('closed', () => writeLog('INFO', '[main] window closed'))
// }

// app.whenReady().then(() => {
//   writeLog('INFO', `[main] app ready — userData=${app.getPath('userData')}`)
//   session.defaultSession.setPermissionRequestHandler((_wc, permission, cb) => {
//     const granted = permission === 'media'
//     writeLog('INFO', `[main] permission request permission=${permission} granted=${granted}`)
//     cb(granted)
//   })
//   createWindow()
//   app.on('activate', () => {
//     writeLog('INFO', '[main] app activate')
//     if (BrowserWindow.getAllWindows().length === 0) createWindow()
//   })
// })

// app.on('window-all-closed', () => {
//   writeLog('INFO', '[main] all windows closed — shutting down')
//   stopVirtualCamera()
//   if (process.platform !== 'darwin') app.quit()
// })

// app.on('before-quit', () => writeLog('INFO', '[main] before-quit'))
// app.on('quit', (_e, code) => writeLog('INFO', `[main] quit exitCode=${code}`))

// // ── IPC handlers ──────────────────────────────────────────────────────────────

// ipcMain.handle('vcam:start', async () => {
//   writeLog('INFO', '[ipc] vcam:start called')
//   try {
//     const result = startVirtualCamera()
//     writeLog(result.ok ? 'INFO' : 'WARN', `[ipc] vcam:start result ok=${result.ok} obs=${result.obs ?? false}${result.error ? ` error="${result.error}"` : ''}`)
//     return result
//   } catch (e: unknown) {
//     const msg = e instanceof Error ? e.message : String(e)
//     writeLog('ERROR', `[ipc] vcam:start threw: ${msg}`)
//     return { ok: false, obs: false, error: msg }
//   }
// })

// ipcMain.handle('vcam:stop', async () => {
//   writeLog('INFO', '[ipc] vcam:stop called')
//   try {
//     stopVirtualCamera()
//     writeLog('INFO', '[ipc] vcam:stop done')
//   } catch (e: unknown) {
//     writeLog('WARN', `[ipc] vcam:stop threw: ${e instanceof Error ? e.message : String(e)}`)
//   }
// })

// let firstFrameLogged = false
// ipcMain.handle('vcam:pushFrame', (_e, width: number, height: number, rgba: Uint8Array) => {
//   if (!firstFrameLogged) {
//     firstFrameLogged = true
//     writeLog('INFO', `[ipc] vcam:pushFrame — first frame received ${width}x${height} bytes=${rgba?.byteLength ?? 'n/a'}`)
//   }
//   if (!width || !height || !rgba?.byteLength) {
//     writeLog('WARN', `[ipc] vcam:pushFrame — invalid args width=${width} height=${height} bytes=${rgba?.byteLength ?? 'n/a'}`)
//     return
//   }
//   try {
//     pushFrame(width, height, Buffer.from(rgba.buffer, rgba.byteOffset, rgba.byteLength))
//   } catch (e: unknown) {
//     writeLog('WARN', `[ipc] vcam:pushFrame threw ${width}x${height}: ${e instanceof Error ? e.message : String(e)}`)
//   }
// })

// ipcMain.handle('log', (_e, level: string, message: string) => {
//   writeLog(level as 'INFO' | 'WARN' | 'ERROR', '[renderer]', message)
// })

// ipcMain.handle('getLogPath', () => {
//   writeLog('INFO', '[ipc] getLogPath called')
//   return LOG_PATH
// })

import { app, BrowserWindow, ipcMain, Menu, Tray, nativeImage, session } from 'electron'
import path from 'path'
import fs from 'fs'
import { startVirtualCamera, stopVirtualCamera, pushFrame } from './vcam'

// Must be set before app.whenReady() — prevents RAF/timer throttling when window is hidden
app.commandLine.appendSwitch('disable-background-timer-throttling')
app.commandLine.appendSwitch('disable-renderer-backgrounding')
app.commandLine.appendSwitch('disable-backgrounding-occluded-windows')

const isDev = !app.isPackaged

let isQuitting = false

// ── File logger ───────────────────────────────────────────────────────────────
const LOG_PATH      = path.join(app.getPath('userData'), 'peercam.log')
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
writeLog('INFO', `PeerCam starting v${app.getVersion()} pid=${process.pid} platform=${process.platform} arch=${process.arch} electron=${process.versions.electron} node=${process.versions.node} modules_abi=${process.versions.modules}`)
writeLog('INFO', `log file: ${LOG_PATH}`)
writeLog('INFO', `resourcesPath=${process.resourcesPath ?? 'n/a'} isPackaged=${app.isPackaged}`)

const _consoleLog   = console.log.bind(console)
const _consoleWarn  = console.warn.bind(console)
const _consoleError = console.error.bind(console)
console.log   = (...a) => { _consoleLog(...a);   writeLog('INFO',  ...a) }
console.warn  = (...a) => { _consoleWarn(...a);  writeLog('WARN',  ...a) }
console.error = (...a) => { _consoleError(...a); writeLog('ERROR', ...a) }

// ── Process-level error traps ─────────────────────────────────────────────────
process.on('uncaughtException', (err, origin) => {
  writeLog('ERROR', `[main] uncaughtException origin=${origin} message="${err.message}"\n${err.stack ?? ''}`)
})
process.on('unhandledRejection', (reason, promise) => {
  void promise
  writeLog('ERROR', `[main] unhandledRejection: ${reason instanceof Error ? reason.stack ?? reason.message : String(reason)}`)
})
process.on('SIGTERM', () => writeLog('WARN', '[main] SIGTERM received'))
process.on('SIGINT',  () => writeLog('WARN', '[main] SIGINT received'))

// ── Window ────────────────────────────────────────────────────────────────────
function createWindow() {
  writeLog('INFO', '[main] creating BrowserWindow')
  mainWindow = new BrowserWindow({
    width: 480,
    height: 680,
    resizable: false,
    title: 'PeerCam',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false,  // keep RAF running when window is hidden/minimised
    },
  })

  if (isDev) {
    writeLog('INFO', '[main] loading dev URL http://localhost:5173')
    mainWindow.loadURL('http://localhost:5173')
  } else {
    const htmlPath = path.join(__dirname, '../renderer/index.html')
    writeLog('INFO', `[main] loading file ${htmlPath}`)
    mainWindow.loadFile(htmlPath)
  }

  mainWindow.webContents.on('did-finish-load', () =>
    writeLog('INFO', '[main] renderer did-finish-load'))

  mainWindow.webContents.on('did-fail-load', (_e, code, desc, url) =>
    writeLog('ERROR', `[main] renderer did-fail-load code=${code} desc="${desc}" url=${url}`))

  mainWindow.webContents.on('render-process-gone', (_e, details) => {
    writeLog('ERROR', `[main] renderer process gone — reason=${details.reason} exitCode=${details.exitCode}`)
    writeLog('ERROR', `[main] renderer crash details: ${JSON.stringify(details)}`)
  })

  mainWindow.webContents.on('unresponsive', () =>
    writeLog('WARN', '[main] renderer became unresponsive'))

  mainWindow.webContents.on('responsive', () =>
    writeLog('INFO', '[main] renderer became responsive again'))

  mainWindow.webContents.on('console-message', (_e, level, message, line, sourceId) => {
    const lvl = level === 3 ? 'ERROR' : level === 2 ? 'WARN' : 'INFO'
    writeLog(lvl, '[renderer]', message, sourceId ? `(${sourceId}:${line})` : '')
  })

  // Hide to tray instead of closing — keeps the renderer alive so the frame
  // pump and WebRTC connection continue running with no interruption.
  mainWindow.on('close', (event) => {
    writeLog('INFO', '[main] window close event')
    if (!isQuitting) {
      event.preventDefault()
      mainWindow?.hide()
      writeLog('INFO', '[main] window hidden to tray — vcam pipe still active')
    }
  })

  mainWindow.on('closed', () => writeLog('INFO', '[main] window closed'))
}

// ── Tray ──────────────────────────────────────────────────────────────────────
let tray: Tray | null = null
let mainWindow: BrowserWindow | null = null

// Whether the user has opted to hide the tray icon while connected.
// Even when true the app is fully alive; the icon is simply not shown.
let trayHidden = false

function showWindow() {
  if (!mainWindow) return
  mainWindow.show()
  mainWindow.focus()
  writeLog('INFO', '[tray] window restored')
}

function buildTrayMenu() {
  return Menu.buildFromTemplate([
    {
      label: 'Show PeerCam',
      click: () => showWindow(),
    },
    { type: 'separator' },
    {
      label: trayHidden ? 'Show tray icon' : 'Hide tray icon while connected',
      click: () => {
        trayHidden = !trayHidden
        if (trayHidden) {
          tray?.setImage(nativeImage.createEmpty())
          writeLog('INFO', '[tray] icon hidden by user — app still running')
        } else {
          const iconPath = path.join(__dirname, '../../public/window.svg')
          tray?.setImage(nativeImage.createFromPath(iconPath))
          writeLog('INFO', '[tray] icon restored')
        }
        // Rebuild the menu so the label flips
        tray?.setContextMenu(buildTrayMenu())
      },
    },
    { type: 'separator' },
    {
      label: 'Quit PeerCam',
      click: () => {
        writeLog('INFO', '[tray] quit clicked')
        isQuitting = true
        app.quit()
      },
    },
  ])
}

function createTray() {
  const iconPath = path.join(__dirname, '../../public/window.svg')
  const icon = nativeImage.createFromPath(iconPath)
  tray = new Tray(icon)
  tray.setContextMenu(buildTrayMenu())
  tray.setToolTip('PeerCam')

  // Single click on Windows / double-click everywhere → show window
  tray.on('click',        () => { if (process.platform === 'win32') showWindow() })
  tray.on('double-click', () => showWindow())

  writeLog('INFO', '[main] tray created')
}

// ── IPC: tray control from renderer ──────────────────────────────────────────
// Renderer can call window.peercam.setTrayHidden(true) to suppress the tray
// icon while a live session is active, and false to restore it.
ipcMain.handle('tray:setHidden', (_e, hidden: boolean) => {
  trayHidden = hidden
  if (tray) {
    if (hidden) {
      tray.setImage(nativeImage.createEmpty())
    } else {
      const iconPath = path.join(__dirname, '../../public/window.svg')
      tray.setImage(nativeImage.createFromPath(iconPath))
    }
    tray.setContextMenu(buildTrayMenu())
  }
  writeLog('INFO', `[ipc] tray:setHidden hidden=${hidden}`)
})

// ── App lifecycle ─────────────────────────────────────────────────────────────
app.whenReady().then(() => {
  writeLog('INFO', `[main] app ready — userData=${app.getPath('userData')}`)
  session.defaultSession.setPermissionRequestHandler((_wc, permission, cb) => {
    const granted = permission === 'media'
    writeLog('INFO', `[main] permission request permission=${permission} granted=${granted}`)
    cb(granted)
  })
  createWindow()
  createTray()
  app.on('activate', () => {
    writeLog('INFO', '[main] app activate')
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
    else showWindow()
  })
})

app.on('window-all-closed', () => {
  writeLog('INFO', '[main] all windows closed — shutting down')
  stopVirtualCamera()
  if (process.platform !== 'darwin') app.quit()
})

app.on('before-quit', () => {
  writeLog('INFO', '[main] before-quit')
  isQuitting = true
})
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

let firstFrameLogged = false

ipcMain.handle('vcam:pushFrame', (_e, width: number, height: number, rgba: Uint8Array) => {
  if (!firstFrameLogged) {
    firstFrameLogged = true
    writeLog('INFO', `[ipc] vcam:pushFrame — first frame received ${width}x${height} bytes=${rgba?.byteLength ?? 'n/a'}`)
  }
  if (!width || !height || !rgba?.byteLength) {
    writeLog('WARN', `[ipc] vcam:pushFrame — invalid args width=${width} height=${height} bytes=${rgba?.byteLength ?? 'n/a'}`)
    return
  }
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