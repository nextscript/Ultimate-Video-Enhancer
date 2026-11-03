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
!define PROGID "UVEPlayer.Media"            ; bis 1.0.1 (bleibt für bestehende Standard-App-Zuordnungen registriert)
!define PROGID_VIDEO "UVEPlayer.Video"
!define PROGID_AUDIO "UVEPlayer.Audio"
!define PROGID_PLAYLIST "UVEPlayer.Playlist"
!define CAPKEY "Software\${APPNAME}\Capabilities"
; Explorer-Vorschaubilder (UVEThumbnailer.dll, IThumbnailProvider) – nur aktiv, wenn UVE Player Standard-App ist
!define THUMB_CLSID "{4D8C74FB-9580-421B-9221-9D77DFFD7450}"
!define THUMB_IID "{E357FCCD-A995-4576-B01F-234630154E96}"

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

; 64-Bit-Registry-Ansicht: NSIS ist ein 32-Bit-Programm und schrieb bis 1.0.2 nach WOW6432Node – dort findet der
; 64-Bit-Explorer weder den Thumbnail-Provider (CLSID) noch die "Approved"-Freigabe.
Function .onInit
    SetRegView 64
    ReadRegStr $0 HKLM "${APPKEY}" "InstallMode"
    ReadRegStr $1 HKCU "${APPKEY}" "InstallMode"
    ${If} $0 == ""
    ${AndIf} $1 == ""
        SetRegView 32   ; bestehende Installation bis 1.0.2 (32-Bit-Ansicht) für Modus und Ordner erkennen
    ${EndIf}
    !insertmacro MULTIUSER_INIT
    SetRegView 64
FunctionEnd

Function un.onInit
    SetRegView 64
    !insertmacro MULTIUSER_UNINIT
FunctionEnd

; Einträge, die frühere Versionen in die 32-Bit-Ansicht geschrieben haben, entfernen (danach neu in 64 Bit)
!macro CleanWow64 UN
Function ${UN}CleanWow64Registry
    SetRegView 32
    DeleteRegKey SHCTX "Software\Classes\CLSID\${THUMB_CLSID}"
    DeleteRegValue SHCTX "Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" "${THUMB_CLSID}"
    DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXENAME}"
    DeleteRegKey SHCTX "${REGKEY}"
    DeleteRegKey SHCTX "${APPKEY}"
    SetRegView 64
FunctionEnd
!macroend
!insertmacro CleanWow64 ""
!insertmacro CleanWow64 "un."

; Per-user default: %LOCALAPPDATA%\Programs\UVE Player (Windows convention) instead of %LOCALAPPDATA%\UVE Player
Function OnInstallModeChanged
    ${If} $MultiUser.InstallMode == "CurrentUser"
    ${AndIf} $INSTDIR == "$LOCALAPPDATA\${APPNAME}"
        StrCpy $INSTDIR "$LOCALAPPDATA\Programs\${APPNAME}"
    ${EndIf}
FunctionEnd
Function un.OnInstallModeChanged
FunctionEnd


; ---------------------------------------------------------------- Dateizuordnungen
; ProgID: Name, Beschreibung, Thumbnail-Handler (1 = Video-Vorschaubilder)
!macro RegProgId ID DESC THUMB
    WriteRegStr SHCTX "Software\Classes\${ID}" "" "${DESC}"
    WriteRegStr SHCTX "Software\Classes\${ID}" "FriendlyTypeName" "${DESC}"
    WriteRegStr SHCTX "Software\Classes\${ID}\DefaultIcon" "" "$INSTDIR\${EXENAME},0"
    WriteRegStr SHCTX "Software\Classes\${ID}\shell" "" "open"
    WriteRegStr SHCTX "Software\Classes\${ID}\shell\open" "FriendlyAppName" "${APPNAME}"
    WriteRegStr SHCTX "Software\Classes\${ID}\shell\open\command" "" '"$INSTDIR\${EXENAME}" "%1"'
    !if ${THUMB} == 1
        ; Vorschaubild + UVE-Icon in der Ecke des Vorschaubilds (TypeOverlay)
        WriteRegStr SHCTX "Software\Classes\${ID}\ShellEx\${THUMB_IID}" "" "${THUMB_CLSID}"
        WriteRegStr SHCTX "Software\Classes\${ID}" "TypeOverlay" "$INSTDIR\${EXENAME},0"
    !endif
!macroend

; Erweiterung → „Öffnen mit“ (OpenWithProgids) + Standard-Apps (Capabilities\FileAssociations)
!macro RegExt EXT ID PERCEIVED
    WriteRegStr SHCTX "Software\Classes\.${EXT}\OpenWithProgids" "${ID}" ""
    DeleteRegValue SHCTX "Software\Classes\.${EXT}\OpenWithProgids" "${PROGID}"   ; sonst doppelt in „Öffnen mit“
    WriteRegStr SHCTX "Software\Classes\Applications\${EXENAME}\SupportedTypes" ".${EXT}" ""
    WriteRegStr SHCTX "${CAPKEY}\FileAssociations" ".${EXT}" "${ID}"
    ; Wahrgenommenen Typ nur setzen, wenn Windows keinen kennt (z. B. .mkv auf älteren Systemen)
    ReadRegStr $0 HKCR ".${EXT}" "PerceivedType"
    ${If} $0 == ""
        WriteRegStr SHCTX "Software\Classes\.${EXT}" "PerceivedType" "${PERCEIVED}"
    ${EndIf}
!macroend

!macro UnRegExt EXT ID
    DeleteRegValue SHCTX "Software\Classes\.${EXT}\OpenWithProgids" "${ID}"
    DeleteRegValue SHCTX "Software\Classes\.${EXT}\OpenWithProgids" "${PROGID}"
!macroend

Function RegisterAssociations
    !insertmacro RegProgId "${PROGID_VIDEO}" "Video (UVE Player)" 1
    !insertmacro RegProgId "${PROGID_AUDIO}" "Audio (UVE Player)" 0
    !insertmacro RegProgId "${PROGID_PLAYLIST}" "Playlist (UVE Player)" 0
    ; Alter ProgID aus 1.0.0/1.0.1: falls er schon Standard-App ist, funktioniert er weiter (mit Vorschaubildern)
    !insertmacro RegProgId "${PROGID}" "Media file (UVE Player)" 1

    ; Thumbnail-Provider (COM, in der isolierten Explorer-Umgebung ausgeführt)
    WriteRegStr SHCTX "Software\Classes\CLSID\${THUMB_CLSID}" "" "UVE Player Thumbnail Provider"
    WriteRegStr SHCTX "Software\Classes\CLSID\${THUMB_CLSID}\InprocServer32" "" "$INSTDIR\UVEThumbnailer.dll"
    WriteRegStr SHCTX "Software\Classes\CLSID\${THUMB_CLSID}\InprocServer32" "ThreadingModel" "Apartment"
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" "${THUMB_CLSID}" "UVE Player Thumbnail Provider"

    ; Anwendung (Öffnen mit → Andere App, Standard-Apps in den Windows-Einstellungen)
    WriteRegStr SHCTX "Software\Classes\Applications\${EXENAME}" "FriendlyAppName" "${APPNAME}"
    WriteRegStr SHCTX "Software\Classes\Applications\${EXENAME}\DefaultIcon" "" "$INSTDIR\${EXENAME},0"
    WriteRegStr SHCTX "Software\Classes\Applications\${EXENAME}\shell\open\command" "" '"$INSTDIR\${EXENAME}" "%1"'
    WriteRegStr SHCTX "${CAPKEY}" "ApplicationName" "${APPNAME}"
    WriteRegStr SHCTX "${CAPKEY}" "ApplicationDescription" "Media player with libmpv and the Ultimate Video Enhancer"
    WriteRegStr SHCTX "${CAPKEY}" "ApplicationIcon" "$INSTDIR\${EXENAME},0"
    WriteRegStr SHCTX "Software\RegisteredApplications" "${APPNAME}" "${CAPKEY}"
    WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXENAME}" "" "$INSTDIR\${EXENAME}"

    !insertmacro RegExt "mp4" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "m4v" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "mkv" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "mk3d" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "webm" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "mov" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "avi" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "wmv" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "flv" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "ts" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "m2ts" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "mts" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "mpg" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "mpeg" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "3gp" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "ogv" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "vob" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "divx" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "rm" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "rmvb" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "asf" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "f4v" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "hevc" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "264" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "265" "${PROGID_VIDEO}" "video"
    !insertmacro RegExt "mp3" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "m4a" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "aac" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "flac" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "wav" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "ogg" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "oga" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "opus" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "wma" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "ac3" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "eac3" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "dts" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "mka" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "aiff" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "alac" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "ape" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "wv" "${PROGID_AUDIO}" "audio"
    !insertmacro RegExt "m3u" "${PROGID_PLAYLIST}" "text"
    !insertmacro RegExt "m3u8" "${PROGID_PLAYLIST}" "text"
    !insertmacro RegExt "pls" "${PROGID_PLAYLIST}" "text"
    ; Explorer neu einlesen lassen (Symbole, „Öffnen mit“, Vorschaubilder)
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
FunctionEnd

Function un.UnregisterAssociations
    !insertmacro UnRegExt "mp4" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "m4v" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "mkv" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "mk3d" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "webm" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "mov" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "avi" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "wmv" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "flv" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "ts" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "m2ts" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "mts" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "mpg" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "mpeg" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "3gp" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "ogv" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "vob" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "divx" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "rm" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "rmvb" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "asf" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "f4v" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "hevc" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "264" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "265" "${PROGID_VIDEO}"
    !insertmacro UnRegExt "mp3" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "m4a" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "aac" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "flac" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "wav" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "ogg" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "oga" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "opus" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "wma" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "ac3" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "eac3" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "dts" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "mka" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "aiff" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "alac" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "ape" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "wv" "${PROGID_AUDIO}"
    !insertmacro UnRegExt "m3u" "${PROGID_PLAYLIST}"
    !insertmacro UnRegExt "m3u8" "${PROGID_PLAYLIST}"
    !insertmacro UnRegExt "pls" "${PROGID_PLAYLIST}"
    DeleteRegKey SHCTX "Software\Classes\${PROGID}"
    DeleteRegKey SHCTX "Software\Classes\${PROGID_VIDEO}"
    DeleteRegKey SHCTX "Software\Classes\${PROGID_AUDIO}"
    DeleteRegKey SHCTX "Software\Classes\${PROGID_PLAYLIST}"
    DeleteRegKey SHCTX "Software\Classes\Applications\${EXENAME}"
    DeleteRegKey SHCTX "Software\Classes\CLSID\${THUMB_CLSID}"
    DeleteRegValue SHCTX "Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved" "${THUMB_CLSID}"
    DeleteRegValue SHCTX "Software\RegisteredApplications" "${APPNAME}"
    DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\App Paths\${EXENAME}"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
FunctionEnd

; Der Explorer lädt UVEThumbnailer.dll (und damit mpv\libmpv-2.dll) in seine Vorschau-Hilfsprozesse (dllhost.exe).
; Diese Prozesse gezielt beenden – Windows startet sie bei Bedarf selbst neu. Ohne Neustart des PCs.
; 64-Bit-PowerShell über Sysnative (der Installer ist ein 32-Bit-Prozess und sähe sonst die Module nicht).
!macro ReleaseThumbnailHost UN
Function ${UN}ReleaseThumbnailHost
    StrCpy $0 "$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
    ${IfNot} ${FileExists} "$0"
        StrCpy $0 "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe"
    ${EndIf}
    nsExec::Exec `"$0" -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "Get-Process dllhost -ErrorAction SilentlyContinue | Where-Object { try { @($$_.Modules | Where-Object { $$_.ModuleName -eq 'UVEThumbnailer.dll' }).Count -gt 0 } catch { $$false } } | Stop-Process -Force -ErrorAction SilentlyContinue"`
    Pop $0
    Sleep 300
FunctionEnd
!macroend
!insertmacro ReleaseThumbnailHost ""
!insertmacro ReleaseThumbnailHost "un."

; Falls eine Datei trotzdem noch gesperrt ist: umbenennen (geht auch bei geladenen DLLs) und beim nächsten
; Update aufräumen – kein /REBOOTOK, damit der Installer nie einen Neustart verlangt
!macro MoveInUse FILE
    Delete "${FILE}.old"
    ${If} ${FileExists} "${FILE}"
        Delete "${FILE}"
        ${If} ${FileExists} "${FILE}"
            Rename "${FILE}" "${FILE}.old"
        ${EndIf}
    ${EndIf}
!macroend

Section "UVE Player (required)" SecMain
    SectionIn RO
    Call CleanWow64Registry
    SetOutPath "$INSTDIR"
    Call ReleaseThumbnailHost
    !insertmacro MoveInUse "$INSTDIR\UVEThumbnailer.dll"
    !insertmacro MoveInUse "$INSTDIR\mpv\libmpv-2.dll"
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
    ; „Öffnen mit → UVE Player“ für alle unterstützten Formate (immer; bestehende Standard-Apps bleiben unverändert)
    Call RegisterAssociations
    SetRebootFlag false   ; ein Neustart des PCs ist nie nötig
SectionEnd

Section "Desktop shortcut" SecDesktop
    CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXENAME}"
SectionEnd


Section "Uninstall"
    Delete "$DESKTOP\${APPNAME}.lnk"
    RMDir /r "$SMPROGRAMS\${APPNAME}"
    Call un.UnregisterAssociations
    Call un.CleanWow64Registry
    DeleteRegKey SHCTX "${REGKEY}"
    DeleteRegKey SHCTX "${APPKEY}"
    ; User data (%APPDATA%\nextscript\UVE Player: settings, presets, own languages) is kept
    ; Vorschau-Hilfsprozesse des Explorers freigeben (Zuordnungen sind schon entfernt, sie laden die DLL nicht neu)
    Call un.ReleaseThumbnailHost
    RMDir /r "$INSTDIR"
    ; Nie einen Neustart verlangen: sollte noch etwas gesperrt sein, bleibt höchstens ein leerer Rest-Ordner
    SetRebootFlag false
SectionEnd

LangString DESC_Main ${LANG_ENGLISH} "Player, libmpv playback engine, yt-dlp, languages and plugins (Ultimate Video Enhancer, sleep timer). Adds UVE Player to “Open with” for all supported video, audio and playlist files and shows video thumbnails when UVE Player is the default app."
LangString DESC_Desktop ${LANG_ENGLISH} "Create a shortcut on the desktop."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_Main)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_Desktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
