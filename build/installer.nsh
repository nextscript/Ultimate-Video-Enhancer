; UVE Manager additions to the electron-builder NSIS installer (picked up automatically from build/installer.nsh).
;
; Updating over an installed version
; ----------------------------------
; electron-builder's default update runs the old version's uninstaller (copied to %TEMP% and started from there)
; and finds/closes the running app through PowerShell (Get-CimInstance / Stop-Process). Behaviour-based virus
; scanners – Bitdefender Advanced Threat Defense in particular – block exactly that: the old uninstaller is put in
; quarantine, the setup retries five times and ends with "UVE Manager cannot be closed".
;
; So this installer does the update itself:
;   - the running app is closed with the nsProcess plugin (no PowerShell, no extra processes),
;   - the old uninstaller is not started; the old program files are replaced in place,
;   - the tools folder of the old version (FFmpeg, REAL-Video-Enhancer – several GB) is never deleted.
; The uninstaller still closes the app itself when the user uninstalls; started by an updating installer
; ("--updated") it does nothing extra.
;
; Tools: since 1.0.5 the installed app keeps them in %LOCALAPPDATA%\UVE Manager\tools (electron/store.ts
; installedToolsDir). Tools that 1.0.4 kept in the program folder are moved there with a plain rename when they are
; on the same drive; on another drive they stay where they are and the app keeps using them.
; Settings live in %APPDATA%\UVE Manager and are not touched by updates.

!ifndef nsProcess::FindProcess
  !include "nsProcess.nsh"
!endif

!macro customCheckAppRunning
  !ifdef BUILD_UNINSTALLER
    ${ifNot} ${isUpdated}
      !insertmacro uveCloseApp
    ${endIf}
  !else
    !insertmacro uveCloseApp
    !insertmacro uveTakeOverOldVersion
  !endif
!macroend

; Closes a running UVE Manager: ask, close the window (the app stops its FFmpeg / Python jobs), force after 5 s.
!macro uveCloseApp
  ${nsProcess::FindProcess} "${APP_EXECUTABLE_FILENAME}" $R0
  ${if} $R0 == 0
    ${ifNot} ${isUpdated}
      MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION "$(appRunning)" /SD IDOK IDOK uveDoClose
      Quit
    ${endIf}
    uveDoClose:
    DetailPrint "$(appClosing)"
    ${nsProcess::CloseProcess} "${APP_EXECUTABLE_FILENAME}" $R0
    StrCpy $R1 0
    ${Do}
      Sleep 500
      ${nsProcess::FindProcess} "${APP_EXECUTABLE_FILENAME}" $R0
      ${If} $R0 != 0
        ${Break}
      ${EndIf}
      IntOp $R1 $R1 + 1
      ${If} $R1 == 10
        ${nsProcess::KillProcess} "${APP_EXECUTABLE_FILENAME}" $R0
      ${ElseIf} $R1 >= 20
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(appCannotBeClosed)" /SD IDCANCEL IDRETRY uveRetryClose
        Quit
        uveRetryClose:
        StrCpy $R1 0
      ${EndIf}
    ${Loop}
    ; give Windows a moment to release the program files
    Sleep 1000
  ${endIf}
  ${nsProcess::Unload}
!macroend

; Replaces the old version in place instead of running its uninstaller (see above).
!macro uveTakeOverOldVersion
  ReadRegStr $R8 SHELL_CONTEXT "${INSTALL_REGISTRY_KEY}" InstallLocation
  ${If} $R8 != ""
  ${AndIf} ${FileExists} "$R8\${APP_EXECUTABLE_FILENAME}"
    ReadEnvStr $R9 LOCALAPPDATA
    ${If} $R9 != ""
      !insertmacro uveKeepTool "$R8" "$R9\UVE Manager\tools" "rve"
      !insertmacro uveKeepTool "$R8" "$R9\UVE Manager\tools" "ffmpeg"
    ${EndIf}
    ; files of the old version that the new one does not simply overwrite; tools\ stays untouched
    RMDir /r "$R8\resources"
    RMDir /r "$R8\locales"
    Delete "$R8\${UNINSTALL_FILENAME}"
  ${EndIf}
  ; without an UninstallString electron-builder does not start the old uninstaller; the new values are written
  ; again at the end of the installation
  DeleteRegValue SHELL_CONTEXT "${UNINSTALL_REGISTRY_KEY}" UninstallString
  DeleteRegValue SHELL_CONTEXT "${UNINSTALL_REGISTRY_KEY}" QuietUninstallString
  DeleteRegValue HKEY_CURRENT_USER "${UNINSTALL_REGISTRY_KEY}" UninstallString
  DeleteRegValue HKEY_CURRENT_USER "${UNINSTALL_REGISTRY_KEY}" QuietUninstallString
  !ifdef UNINSTALL_REGISTRY_KEY_2
    DeleteRegValue SHELL_CONTEXT "${UNINSTALL_REGISTRY_KEY_2}" UninstallString
    DeleteRegValue SHELL_CONTEXT "${UNINSTALL_REGISTRY_KEY_2}" QuietUninstallString
    DeleteRegValue HKEY_CURRENT_USER "${UNINSTALL_REGISTRY_KEY_2}" UninstallString
    DeleteRegValue HKEY_CURRENT_USER "${UNINSTALL_REGISTRY_KEY_2}" QuietUninstallString
  !endif
  ClearErrors
!macroend

; Renames OLD\tools\NAME to TARGET\NAME unless TARGET already has it (same rule as adoptLegacyTools in the app).
; Only a rename – no copying of thousands of files. On another drive the folder stays where it is.
!macro uveKeepTool OLD TARGET NAME
  ${If} ${FileExists} "${OLD}\tools\${NAME}\*.*"
  ${AndIfNot} ${FileExists} "${TARGET}\${NAME}\*.*"
    CreateDirectory "${TARGET}"
    ClearErrors
    Rename "${OLD}\tools\${NAME}" "${TARGET}\${NAME}"
    ${If} ${Errors}
      DetailPrint "Keeping ${NAME} in ${OLD}\tools"
    ${Else}
      DetailPrint "Moved ${NAME}: ${OLD}\tools\${NAME} -> ${TARGET}\${NAME}"
    ${EndIf}
    ClearErrors
  ${EndIf}
!macroend
