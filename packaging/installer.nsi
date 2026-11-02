; UVE Player – Windows installer
; Install for all users (Program Files, requires administrator rights) or only for the current user (no admin rights).
; Usage: makensis /DVERSION=1.0.0 /DSTAGE=<folder> /DOUTFILE=<setup.exe> installer.nsi
; Silent: setup.exe /S /AllUsers   or   setup.exe /S /CurrentUser
Unicode true
SetCompressor /SOLID lzma

!define APPNAME "UVE Player"
!define EXENAME "UVEPlayer.exe"
!define PUBLISHER "nextscript"
!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\UVEPlayer"
!define APPKEY "Software\${APPNAME}"
!define PROGID "UVEPlayer.Media"

; ---------------------------------------------------------------- Multi-user (all users / current user)
!define MULTIUSER_EXECUTIONLEVEL Highest           ; admins may choose "all users", standard users install per user
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE           ; /AllUsers /CurrentUser
!define MULTIUSER_USE_PROGRAMFILES64
!define MULTIUSER_INSTALLMODE_INSTDIR "${APPNAME}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "${APPKEY}"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME "InstallDir"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "${APPKEY}"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME "InstallMode"
!define MULTIUSER_INSTALLMODE_FUNCTION OnInstallModeChanged
!define MULTIUSER_INSTALLMODE_UNFUNCTION un.OnInstallModeChanged

!include "MultiUser.nsh"
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
BrandingText "${APPNAME} ${VERSION}"

!define MUI_ICON "${STAGE}\icon.ico"
!define MUI_UNICON "${STAGE}\icon.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXENAME}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch UVE Player"
!define MUI_WELCOMEPAGE_TEXT "This wizard installs ${APPNAME} ${VERSION} – a media player with libmpv, network streams (YouTube, Twitch, HLS, TCP) and the Ultimate Video Enhancer effects.$\r$\n$\r$\nlibmpv, yt-dlp and all other tools are included; nothing else needs to be installed.$\r$\n$\r$\nClick Next to continue."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\docs\THIRD_PARTY_LICENSES.txt"
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "${APPNAME} Setup"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "see docs\THIRD_PARTY_LICENSES.txt"

Function .onInit
    !insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
    !insertmacro MULTIUSER_UNINIT
FunctionEnd

; Per-user default: %LOCALAPPDATA%\Programs\UVE Player (Windows convention) instead of %LOCALAPPDATA%\UVE Player
Function OnInstallModeChanged
    ${If} $MultiUser.InstallMode == "CurrentUser"
    ${AndIf} $INSTDIR == "$LOCALAPPDATA\${APPNAME}"
        StrCpy $INSTDIR "$LOCALAPPDATA\Programs\${APPNAME}"
    ${EndIf}
FunctionEnd
Function un.OnInstallModeChanged
FunctionEnd

Section "UVE Player (required)" SecMain
    SectionIn RO
    SetOutPath "$INSTDIR"
    File /r "${STAGE}\*.*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    ; SHCTX = HKLM (all users) or HKCU (current user), set by MultiUser
    WriteRegStr SHCTX "${APPKEY}" "InstallDir" "$INSTDIR"
    WriteRegStr SHCTX "${APPKEY}" "InstallMode" "$MultiUser.InstallMode"
    WriteRegStr SHCTX "${REGKEY}" "DisplayName" "${APPNAME}"
    WriteRegStr SHCTX "${REGKEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr SHCTX "${REGKEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr SHCTX "${REGKEY}" "DisplayIcon" "$INSTDIR\${EXENAME}"
    WriteRegStr SHCTX "${REGKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe" /$MultiUser.InstallMode'
    WriteRegStr SHCTX "${REGKEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /$MultiUser.InstallMode /S'
    WriteRegStr SHCTX "${REGKEY}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD SHCTX "${REGKEY}" "NoModify" 1
    WriteRegDWORD SHCTX "${REGKEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD SHCTX "${REGKEY}" "EstimatedSize" $0
    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${EXENAME}"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\${APPNAME} (Safe Mode).lnk" "$INSTDIR\${EXENAME}" "--safe-mode"
    CreateShortCut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\Uninstall.exe" "/$MultiUser.InstallMode"
SectionEnd

Section "Desktop shortcut" SecDesktop
    CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXENAME}"
SectionEnd

Section "Add to the “Open with” menu (does not change default apps)" SecAssoc
    WriteRegStr SHCTX "Software\Classes\${PROGID}" "" "Media file (UVE Player)"
    WriteRegStr SHCTX "Software\Classes\${PROGID}\DefaultIcon" "" "$INSTDIR\${EXENAME},0"
    WriteRegStr SHCTX "Software\Classes\${PROGID}\shell\open\command" "" '"$INSTDIR\${EXENAME}" "%1"'
    WriteRegStr SHCTX "Software\Classes\Applications\${EXENAME}\shell\open\command" "" '"$INSTDIR\${EXENAME}" "%1"'
    !macro OpenWith EXT
        WriteRegStr SHCTX "Software\Classes\.${EXT}\OpenWithProgids" "${PROGID}" ""
    !macroend
    !insertmacro OpenWith "mp4"
    !insertmacro OpenWith "mkv"
    !insertmacro OpenWith "webm"
    !insertmacro OpenWith "mov"
    !insertmacro OpenWith "avi"
    !insertmacro OpenWith "wmv"
    !insertmacro OpenWith "flv"
    !insertmacro OpenWith "ts"
    !insertmacro OpenWith "m2ts"
    !insertmacro OpenWith "mpg"
    !insertmacro OpenWith "m4v"
    !insertmacro OpenWith "mp3"
    !insertmacro OpenWith "flac"
    !insertmacro OpenWith "m4a"
    !insertmacro OpenWith "ogg"
    !insertmacro OpenWith "opus"
    !insertmacro OpenWith "wav"
    !insertmacro OpenWith "m3u"
    !insertmacro OpenWith "m3u8"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
SectionEnd

Section "Uninstall"
    Delete "$DESKTOP\${APPNAME}.lnk"
    RMDir /r "$SMPROGRAMS\${APPNAME}"
    DeleteRegKey SHCTX "Software\Classes\${PROGID}"
    DeleteRegKey SHCTX "Software\Classes\Applications\${EXENAME}"
    !macro RemoveOpenWith EXT
        DeleteRegValue SHCTX "Software\Classes\.${EXT}\OpenWithProgids" "${PROGID}"
    !macroend
    !insertmacro RemoveOpenWith "mp4"
    !insertmacro RemoveOpenWith "mkv"
    !insertmacro RemoveOpenWith "webm"
    !insertmacro RemoveOpenWith "mov"
    !insertmacro RemoveOpenWith "avi"
    !insertmacro RemoveOpenWith "wmv"
    !insertmacro RemoveOpenWith "flv"
    !insertmacro RemoveOpenWith "ts"
    !insertmacro RemoveOpenWith "m2ts"
    !insertmacro RemoveOpenWith "mpg"
    !insertmacro RemoveOpenWith "m4v"
    !insertmacro RemoveOpenWith "mp3"
    !insertmacro RemoveOpenWith "flac"
    !insertmacro RemoveOpenWith "m4a"
    !insertmacro RemoveOpenWith "ogg"
    !insertmacro RemoveOpenWith "opus"
    !insertmacro RemoveOpenWith "wav"
    !insertmacro RemoveOpenWith "m3u"
    !insertmacro RemoveOpenWith "m3u8"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    DeleteRegKey SHCTX "${REGKEY}"
    DeleteRegKey SHCTX "${APPKEY}"
    ; User data (%APPDATA%\nextscript\UVE Player: settings, presets, own languages) is kept
    RMDir /r "$INSTDIR"
SectionEnd

LangString DESC_Main ${LANG_ENGLISH} "Player, libmpv playback engine, yt-dlp, languages and plugins (Ultimate Video Enhancer, sleep timer)."
LangString DESC_Desktop ${LANG_ENGLISH} "Create a shortcut on the desktop."
LangString DESC_Assoc ${LANG_ENGLISH} "Offer UVE Player in the “Open with” context menu for video, audio and playlist files. Existing default apps stay unchanged."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_Main)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desktop)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecAssoc} $(DESC_Assoc)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
