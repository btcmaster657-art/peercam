; PeerCam installer/uninstaller hooks

; ── Kill all PeerCam processes cleanly ───────────────────────────────────────
!macro KillPeerCam
  ; 1. Ask the running app to quit gracefully
  nsExec::ExecToLog '$SYSDIR\WindowsPowerShell\v1.0\powershell.exe -NonInteractive -WindowStyle Hidden -Command "try { Invoke-WebRequest -Uri http://127.0.0.1:7654/quit -Method POST -TimeoutSec 3 -UseBasicParsing | Out-Null } catch {}"'
  Sleep 2500

  ; 2. Force-kill any remaining Electron processes
  nsExec::ExecToLog '$SYSDIR\taskkill.exe /F /IM "PeerCam.exe" /T'
  nsExec::ExecToLog '$SYSDIR\taskkill.exe /F /IM "PeerCam Helper.exe"'
  nsExec::ExecToLog '$SYSDIR\taskkill.exe /F /IM "PeerCam Helper (GPU).exe"'
  nsExec::ExecToLog '$SYSDIR\taskkill.exe /F /IM "PeerCam Helper (Renderer).exe"'
  nsExec::ExecToLog '$SYSDIR\taskkill.exe /F /IM "PeerCam Helper (Plugin).exe"'

  ; 3. Poll until process is gone (up to 10s)
  StrCpy $R1 0
  ${Do}
    Sleep 500
    nsExec::ExecToStack '$SYSDIR\tasklist.exe /FI "IMAGENAME eq PeerCam.exe" /NH'
    Pop $R2
    ${If} $R2 == ""
      ${Break}
    ${EndIf}
    ${If} $R2 == "INFO: No tasks are running which match the specified criteria."
      ${Break}
    ${EndIf}
    IntOp $R1 $R1 + 1
    ${If} $R1 >= 20
      ${Break}
    ${EndIf}
  ${Loop}
!macroend

; ── Fires at the very start of the installer — before any page is shown ──────
!macro customInit
  !insertmacro KillPeerCam
!macroend

; ── Remove all PeerCam data and registry artifacts ───────────────────────────
!macro CleanPeerCamArtifacts
  ; Unregister virtual camera DLL before removing files
  nsExec::ExecToLog '$SYSDIR\regsvr32.exe /s /u "$INSTDIR\resources\PeerCamVCam.dll"'

  ; Electron userData (config, logs, cache)
  RMDir /r "$APPDATA\peercam-desktop"
  RMDir /r "$APPDATA\PeerCam"

  ; Updater/cache folders
  RMDir /r "$LOCALAPPDATA\peercam-desktop-updater"
  RMDir /r "$LOCALAPPDATA\PeerCam-updater"

  ; Login item written by app.setLoginItemSettings
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "PeerCam"
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "peercam-desktop"

  ; Startup shortcut
  Delete "$APPDATA\Microsoft\Windows\Start Menu\Programs\Startup\PeerCam.lnk"
  RMDir /r "$SMPROGRAMS\PeerCam"
!macroend

!macro CleanPeerCamInstallDirs
  RMDir /r "$LOCALAPPDATA\Programs\PeerCam"
  RMDir /r "$LOCALAPPDATA\Programs\peercam-desktop"
  RMDir /r "$PROGRAMFILES\PeerCam"
  RMDir /r "$PROGRAMFILES64\PeerCam"
!macroend

; ── customInstall — remove previous version before installing new one ─────────
!macro customInstall
  ; Process already killed in customInit
  ; Silently uninstall previous version if registry entry exists
  ReadRegStr $R0 HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\PeerCam" "UninstallString"
  ${If} $R0 != ""
    DetailPrint "Removing previous version..."
    nsExec::ExecToLog '$R0 /S'
    Sleep 3000
    !insertmacro CleanPeerCamArtifacts
    !insertmacro CleanPeerCamInstallDirs
  ${EndIf}
  ; Register the virtual camera DirectShow filter — makes it appear as a real webcam
  DetailPrint "Registering PeerCam Virtual Camera..."
  nsExec::ExecToLog '$SYSDIR\regsvr32.exe /s "$INSTDIR\resources\PeerCamVCam.dll"'
!macroend

; ── customUnInstall — full cleanup on uninstall ───────────────────────────────
!macro customUnInstall
  DetailPrint "Stopping PeerCam..."
  !insertmacro KillPeerCam
  DetailPrint "Removing PeerCam files and registry entries..."
  !insertmacro CleanPeerCamArtifacts
  !insertmacro CleanPeerCamInstallDirs
!macroend
