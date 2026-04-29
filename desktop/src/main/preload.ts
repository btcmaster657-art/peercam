import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('peercam', {
  vcamStart: () => ipcRenderer.invoke('vcam:start'),
  vcamStop: () => ipcRenderer.invoke('vcam:stop'),
  vcamPushFrame: (width: number, height: number, rgba: Buffer) =>
    ipcRenderer.invoke('vcam:pushFrame', width, height, rgba),
  platform: process.platform,
})
