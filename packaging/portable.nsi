; UVE Player – portable single-file EXE
; Extracts the player to a temporary folder, starts it and cleans up afterwards.
; Data (settings, plugin data, own languages, tool updates) is kept next to the EXE in "UVE Player Data".
; Usage: makensis /DVERSION=1.0.0 /DSTAGE=<folder> /DOUTFILE=<portable.exe> portable.nsi
Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user
SilentInstall silent
AutoCloseWindow true
!include "FileFunc.nsh"
!include "LogicLib.nsh"
Icon "${STAGE}\icon.ico"
Name "UVE Player ${VERSION} (portable)"
OutFile "${OUTFILE}"
LoadLanguageFile "${NSISDIR}\Contrib\Language files\English.nlf"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "UVE Player (portable)"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "UVE Player ${VERSION} – portable edition"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "CompanyName" "nextscript"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "see docs\THIRD_PARTY_LICENSES.txt"

Section
    ; unique folder per start (several windows or crashes do not interfere)
    System::Call 'kernel32::GetTickCount() i .r0'
    StrCpy $1 "$TEMP\UVEPlayer-${VERSION}-$0"
    SetOutPath "$1"
    File /r "${STAGE}\*.*"
    ${IfNot} ${FileExists} "$1\UVEPlayer.exe"
        MessageBox MB_ICONSTOP "UVE Player could not be extracted to $1 (disk full or access denied)."
        SetErrorLevel 2
        Quit
    ${EndIf}
    ; data folder next to the portable EXE
    System::Call 'kernel32::SetEnvironmentVariable(t "UVE_DATA_DIR", t "$EXEDIR\UVE Player Data")'
    ; pass command line arguments (e.g. a file dropped onto the EXE)
    ${GetParameters} $3
    ExecWait '"$1\UVEPlayer.exe" $3' $4
    SetOutPath "$TEMP"
    RMDir /r "$1"
    SetErrorLevel $4
SectionEnd
