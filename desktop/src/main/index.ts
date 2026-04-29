import { app, BrowserWindow, ipcMain, session } from 'electron'
import path from 'path'
import { startVirtualCamera, stopVirtualCamera, pushFrame } from './vcam'

const isDev = !app.isPackaged

function createWindow() {
  const win = new BrowserWindow({
    width: 480,
    height: 640,
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
}

app.whenReady().then(() => {
  // Allow camera access in renderer for WebRTC
  session.defaultSession.setPermissionRequestHandler((_wc, permission, cb) => {
    cb(permission === 'media')
  })

  createWindow()
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
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

// Renderer sends raw RGBA frame as Buffer — fire and forget, no await
ipcMain.handle('vcam:pushFrame', (_e, width: number, height: number, rgba: Buffer) => {
  pushFrame(width, height, rgba)
  // Return undefined synchronously — keeps IPC round-trip minimal
})
