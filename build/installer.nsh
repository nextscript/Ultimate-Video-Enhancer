; UVE Manager additions to the electron-builder NSIS installer (picked up automatically from build/installer.nsh).
;
; Updating over an older version: electron-builder runs the old version's uninstaller first, and that removes the
; whole program folder. Up to 1.0.4 the installed app kept its tools there (tools\ffmpeg, tools\rve – the
; REAL-Video-Enhancer stack is several GB), so every update lost them. Since 1.0.5 the installed app keeps tools
; in %LOCALAPPDATA%\UVE Manager\tools (electron/store.ts installedToolsDir). Right before the old version is
; uninstalled, the old program folder's tools are moved there, so nothing has to be downloaded again.
; Settings live in %APPDATA%\UVE Manager and are not touched by updates.

; customCheckAppRunning replaces the template's running-app check, so its helpers are included here
!include "getProcessInfo.nsh"
Var pid

!macro customCheckAppRunning
  !insertmacro IS_POWERSHELL_AVAILABLE
  !insertmacro _CHECK_APP_RUNNING
  !ifndef BUILD_UNINSTALLER
    !insertmacro uveKeepTools
  !endif
!macroend

!macro uveKeepTools
  ; folder of the installed (old) version, else the folder chosen for this installation
  ReadRegStr $R8 SHELL_CONTEXT "${INSTALL_REGISTRY_KEY}" InstallLocation
  ${If} $R8 == ""
    StrCpy $R8 "$INSTDIR"
  ${EndIf}
  ReadEnvStr $R9 LOCALAPPDATA
  ${If} $R9 != ""
    !insertmacro uveKeepTool "$R8" "$R9\UVE Manager\tools" "rve"
    !insertmacro uveKeepTool "$R8" "$R9\UVE Manager\tools" "ffmpeg"
  ${EndIf}
!macroend

; moves OLD\tools\NAME to TARGET\NAME unless TARGET already has it (same rule as adoptLegacyTools in the app)
!macro uveKeepTool OLD TARGET NAME
  ${If} ${FileExists} "${OLD}\tools\${NAME}\*.*"
  ${AndIfNot} ${FileExists} "${TARGET}\${NAME}\*.*"
    DetailPrint "Keeping ${NAME}: ${OLD}\tools\${NAME} -> ${TARGET}\${NAME}"
    CreateDirectory "${TARGET}"
    ClearErrors
    Rename "${OLD}\tools\${NAME}" "${TARGET}\${NAME}"
    ${If} ${Errors}
      ; another drive or a locked file: copy instead (the old folder is removed with the old version)
      CreateDirectory "${TARGET}\${NAME}"
      CopyFiles /SILENT "${OLD}\tools\${NAME}\*.*" "${TARGET}\${NAME}"
    ${EndIf}
  ${EndIf}
!macroend
