; UVE Manager – portable self-extractor.
; Asks for a target folder, extracts the complete app (incl. tools\) there and writes the
; "portable.txt" marker so the app keeps its settings in <folder>\data. No registry entries,
; no uninstaller, no admin rights. Re-extracting into the same folder updates the app and keeps
; data\ and tools\ffmpeg.
;
; Defines (passed by scripts/build-portable.mjs): VERSION, SRCDIR, OUTFILE, ICON

Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user
ManifestDPIAware true

!include "MUI2.nsh"

Name "UVE Manager ${VERSION} (portable)"
OutFile "${OUTFILE}"
InstallDir "$DESKTOP\UVE Manager"
BrandingText "UVE Manager ${VERSION}"
ShowInstDetails show

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "UVE Manager"
VIAddVersionKey "FileDescription" "UVE Manager portable (self-extracting)"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "UVE Manager"

!define MUI_ICON "${ICON}"
!define MUI_ABORTWARNING
!define MUI_DIRECTORYPAGE_VERIFYONLEAVE

!define MUI_WELCOMEPAGE_TITLE "UVE Manager ${VERSION} – portable"
!define MUI_WELCOMEPAGE_TEXT "$(TXT_WELCOME)"
!insertmacro MUI_PAGE_WELCOME

!define MUI_DIRECTORYPAGE_TEXT_TOP "$(TXT_DIR)"
!define MUI_DIRECTORYPAGE_TEXT_DESTINATION "$(TXT_DEST)"
!insertmacro MUI_PAGE_DIRECTORY

!define MUI_PAGE_HEADER_TEXT "$(TXT_EXTRACTING)"
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\UVE Manager.exe"
!define MUI_FINISHPAGE_RUN_TEXT "$(TXT_RUN)"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "$(TXT_OPENFOLDER)"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
!define MUI_FINISHPAGE_TEXT "$(TXT_FINISH)"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "English"

LangString TXT_WELCOME ${LANG_GERMAN} "Dieses Programm entpackt die portable Version von UVE Manager in einen Ordner Ihrer Wahl.$\r$\n$\r$\nEs wird nichts installiert: keine Registry-Einträge, kein Startmenü, keine Administratorrechte. Einstellungen werden im Unterordner $\"data$\" gespeichert, Tools (FFmpeg) im Unterordner $\"tools$\".$\r$\n$\r$\nZum Entfernen löschen Sie einfach den Ordner."
LangString TXT_WELCOME ${LANG_ENGLISH} "This program extracts the portable version of UVE Manager into a folder of your choice.$\r$\n$\r$\nNothing is installed: no registry entries, no start menu, no admin rights. Settings are stored in the $\"data$\" subfolder, tools (FFmpeg) in the $\"tools$\" subfolder.$\r$\n$\r$\nTo remove it, simply delete the folder."
LangString TXT_DIR ${LANG_GERMAN} "Wählen Sie den Ordner, in den UVE Manager entpackt werden soll (z. B. auf einem USB-Stick). Ein vorhandener UVE-Manager-Ordner wird aktualisiert; Einstellungen und Tools bleiben erhalten."
LangString TXT_DIR ${LANG_ENGLISH} "Choose the folder UVE Manager is extracted to (e.g. on a USB drive). An existing UVE Manager folder is updated; settings and tools are kept."
LangString TXT_DEST ${LANG_GERMAN} "Zielordner"
LangString TXT_DEST ${LANG_ENGLISH} "Destination folder"
LangString TXT_EXTRACTING ${LANG_GERMAN} "UVE Manager wird entpackt…"
LangString TXT_EXTRACTING ${LANG_ENGLISH} "Extracting UVE Manager…"
LangString TXT_RUN ${LANG_GERMAN} "UVE Manager jetzt starten"
LangString TXT_RUN ${LANG_ENGLISH} "Start UVE Manager now"
LangString TXT_OPENFOLDER ${LANG_GERMAN} "Zielordner öffnen"
LangString TXT_OPENFOLDER ${LANG_ENGLISH} "Open the destination folder"
LangString TXT_FINISH ${LANG_GERMAN} "UVE Manager wurde nach $\r$\n$INSTDIR$\r$\nentpackt. Starten Sie die App über $\"UVE Manager.exe$\" in diesem Ordner."
LangString TXT_FINISH ${LANG_ENGLISH} "UVE Manager was extracted to$\r$\n$INSTDIR$\r$\nStart it with $\"UVE Manager.exe$\" in that folder."
LangString TXT_RUNNING ${LANG_GERMAN} "UVE Manager läuft noch aus diesem Ordner. Bitte schließen Sie die App und klicken Sie auf Wiederholen."
LangString TXT_RUNNING ${LANG_ENGLISH} "UVE Manager is still running from this folder. Please close it and click Retry."
LangString TXT_NOTEMPTY ${LANG_GERMAN} "Der Ordner ist nicht leer und enthält keine UVE-Manager-Installation:$\r$\n$INSTDIR$\r$\n$\r$\nTrotzdem dorthin entpacken?"
LangString TXT_NOTEMPTY ${LANG_ENGLISH} "The folder is not empty and does not contain UVE Manager:$\r$\n$INSTDIR$\r$\n$\r$\nExtract there anyway?"

; Warn before mixing the app into an unrelated, non-empty folder
Function CheckTarget
  IfFileExists "$INSTDIR\UVE Manager.exe" ok
  FindFirst $0 $1 "$INSTDIR\*.*"
  loop:
    StrCmp $1 "" empty
    StrCmp $1 "." next
    StrCmp $1 ".." next
    FindClose $0
    MessageBox MB_YESNO|MB_ICONQUESTION "$(TXT_NOTEMPTY)" IDYES ok
    Abort
  next:
    FindNext $0 $1
    Goto loop
  empty:
    FindClose $0
  ok:
FunctionEnd

Section "UVE Manager"
  Call CheckTarget
  SetOutPath "$INSTDIR"
  ; the exe is locked while the app runs from this folder
  retry:
    IfFileExists "$INSTDIR\UVE Manager.exe" 0 extract
    ClearErrors
    FileOpen $0 "$INSTDIR\UVE Manager.exe" a
    IfErrors locked
    FileClose $0
    Goto extract
  locked:
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(TXT_RUNNING)" IDRETRY retry
    Abort
  extract:
    SetOverwrite on
    File /r "${SRCDIR}\*.*"
    CreateDirectory "$INSTDIR\data"
    CreateDirectory "$INSTDIR\tools\ffmpeg"
    FileOpen $1 "$INSTDIR\portable.txt" w
    FileWrite $1 "UVE Manager portable mode - settings are stored in the data folder next to this file.$\r$\n"
    FileClose $1
SectionEnd
