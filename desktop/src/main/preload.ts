import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('peercam', {
  vcamStart:    () => ipcRenderer.invoke('vcam:start'),
  vcamStop:     () => ipcRenderer.invoke('vcam:stop'),
  vcamPushFrame: (width: number, height: number, rgba: Uint8Array) =>
    ipcRenderer.invoke('vcam:pushFrame', width, height, rgba),
  platform:     process.platform,
  log:          (level: string, message: string) => ipcRenderer.invoke('log', level, message),
  getLogPath:   () => ipcRenderer.invoke('getLogPath'),
})
