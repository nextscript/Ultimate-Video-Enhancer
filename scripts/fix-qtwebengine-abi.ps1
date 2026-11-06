# Repariert die QtWebEngine-ABI-Lücke in Qt 6.12.0-Mischinstallationen:
# Die Qt6WebEngineCore.dll der 6.12.0-Vorschau (Juli 2026) importiert aus Qt6Gui.dll den
# OEFFENTLICH构造 QAccessibleInterface-Konstruktor  ??0QAccessibleInterface@@QEAA@XZ  (Q = public).
# Das GA-Qt6Gui.dll (Sep 2026) exportiert denselben (identischen) Konstruktor nur als
# geschuetzt: ??0QAccessibleInterface@@IEAA@XZ  (Ordinal 126, Hint 0x7D, RVA 0x951E0).
# Der Windows-Loader scheitert dann mit 0xC0000139 ("Einsprungpunkt nicht gefunden").
# Dieser Patch schreibt in der Importtafel von Qt6WebEngineCore.dll den Namen des
# Import-Hint/Name-Eintrags von @@QEAA@XZ auf @@IEAA@XZ um (ein Byte) und setzt den Hint
# auf 0x7D. Beide Namen sind derselbe Trivial-Konstruktor - nur der C++-Zugriffsspezifikator
# hat sich in der Qt-Quelle geaendert. Der native Chat-/WebView-Code bleibt unberuehrt.
# Idempotent: ein bereits gepatchter Eintrag wird erkannt und nicht erneut geschrieben.
#
# Aufruf:
#   powershell -ExecutionPolicy Bypass -File scripts\fix-qtwebengine-abi.ps1              (trockener Lauf, Qt-Verzeichnis)
#   powershell -ExecutionPolicy Bypass -File scripts\fix-qtwebengine-abi.ps1 -Apply       (patchen)
#   powershell ... -Apply -Backup                                                         (mit .orig-Sicherung)
#   powershell ... -Files "M:\...\release\stage\Qt6WebEngineCore.dll" -Apply              (zusaetzliche Dateien)
param(
    [string]$QtDir = $(if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { "M:\LAPS\Qt\6.12.0\msvc2022_64" }),
    [string[]]$Files = @(),
    [switch]$Apply,
    [switch]$Backup
)
$ErrorActionPreference = "Stop"

$OLD = '??0QAccessibleInterface@@QEAA@XZ'   # importiert (Juli-Vorschau) - fehlt im GA-Qt6Gui
$NEW = '??0QAccessibleInterface@@IEAA@XZ'   # exportiert (GA) - identischer Thunk, Ordinal 126 / Hint 0x7D
$NEW_HINT = 0x7D
# Index des zu anderenden Bytes innerhalb des Namens: '@@QEAA' -> Position des 'Q'
$NAME_IDX_Q = 25

function ReadBytesAt($fs, [int64]$pos, [int]$n) {
    $fs.Seek($pos, 'Begin') | Out-Null
    $b = New-Object byte[] $n
    $got = $fs.Read($b, 0, $n)
    if ($got -lt $n) { throw "Read truncation bei 0x{0:X}" -f $pos }
    $b
}
function StrAt($fs, [int64]$pos, [int]$max) {
    $b = ReadBytesAt $fs $pos $max
    $i = [Array]::IndexOf($b, [byte]0); if ($i -lt 0) { $i = $max }
    if ($i -eq 0) { return '' }
    [Text.Encoding]::ASCII.GetString($b[0..($i-1)])
}
function Resolve-Rva($sections, [uint32]$rva) {
    foreach ($s in $sections) {
        $size = [Math]::Max($s.VirtualSize, $s.SizeOfRawData)
        if ($rva -ge $s.VirtualAddress -and $rva -lt ($s.VirtualAddress + $size)) { return $s.PointerToRawData + [int64]($rva - $s.VirtualAddress) }
    }
    return [int64]-1
}
function Fix-Core([string]$path) {
    Write-Host "### $path"
    if (-not (Test-Path $path)) { Write-Host "    NICHT GEFUNDEN - uebersprungen"; return }
    $fs = [IO.File]::Open($path, 'Open', 'ReadWrite', 'ReadWrite')
    try {
        $hdr = ReadBytesAt $fs 0 8192
        $peOff = [BitConverter]::ToInt32($hdr, 0x3C)
        $coffOff = $peOff + 4; $optOff = $peOff + 24
        $numSec  = [BitConverter]::ToUInt16($hdr, $coffOff + 2)
        $sizeOpt = [BitConverter]::ToUInt16($hdr, $coffOff + 16)
        $magic   = [BitConverter]::ToUInt16($hdr, $optOff)
        if ($magic -ne 0x20B) { Write-Host "    kein PE32+ (magic={0:X4}) - uebersprungen" -f $magic; return }
        $secStart = $optOff + $sizeOpt
        $sections = @()
        for ($i = 0; $i -lt $numSec; $i++) {
            $o = $secStart + $i * 40
            $sections += [pscustomobject]@{
                VirtualSize    = [BitConverter]::ToUInt32($hdr, $o + 8)
                VirtualAddress = [BitConverter]::ToUInt32($hdr, $o + 12)
                SizeOfRawData  = [BitConverter]::ToUInt32($hdr, $o + 16)
                PointerToRawData = [BitConverter]::ToUInt32($hdr, $o + 20)
            }
        }
        $dataDir = $optOff + 112
        $impRva  = [BitConverter]::ToUInt32($hdr, $dataDir + 8)     # DataDirectory[1] = Import
        $impOff  = Resolve-Rva $sections $impRva
        $hits = New-Object System.Collections.Generic.List[object]
        $pos = $impOff
        while ($true) {
            $desc = ReadBytesAt $fs $pos 20
            $ilt  = [BitConverter]::ToUInt32($desc, 0)
            $nrva = [BitConverter]::ToUInt32($desc, 12)
            $iat  = [BitConverter]::ToUInt32($desc, 16)
            if ($ilt -eq 0 -and $nrva -eq 0 -and $iat -eq 0) { break }
            $mod = StrAt $fs (Resolve-Rva $sections ([uint32]$nrva)) 64
            $tpos = Resolve-Rva $sections ([uint32]$(if ($ilt -ne 0) { $ilt } else { $iat }))
            while ($true) {
                $t = [BitConverter]::ToInt64((ReadBytesAt $fs $tpos 8), 0)
                if ($t -eq 0) { break }
                $tpos += 8
                if ($t -lt 0) { continue }                          # Import uester Ordinal
                $nhOff = Resolve-Rva $sections ([uint32]($t -band 0x7FFFFFFF))
                if ($nhOff -lt 0) { continue }
                $s = StrAt $fs ($nhOff + 2) 64                     # +2: Name nach 2-Byte-Hint
                if ($s -eq $OLD -or $s -eq $NEW) {
                    $q = (ReadBytesAt $fs ($nhOff + 2 + $NAME_IDX_Q) 1)[0]
                    $hint = [BitConverter]::ToUInt16((ReadBytesAt $fs $nhOff 2), 0)
                    $hits.Add([pscustomobject]@{Module=$mod; StructOff=$nhOff; Hint=$hint; Q=$q})
                }
            }
            $pos += 20
        }
        if ($hits.Count -eq 0) {
            Write-Host "    kein problematischer Import gefunden (Datei ist GA-Version oder bereits konsistent)"
            return
        }
        foreach ($h in $hits) {
            $abs = $h.StructOff + 2 + $NAME_IDX_Q
            if ($h.Q -eq 0x49) {
                Write-Host ("    Modul={0}  Struktur=0x{1:X}  bereits gepatcht ({2}) - unveraendert" -f $h.Module, $h.StructOff, $NEW)
                continue
            }
            if ($h.Q -ne 0x51) { Write-Host ("    FEHLER: unerwartetes Byte 0x{0:X2} bei 0x{1:X}" -f $h.Q, $abs); return }
            Write-Host ("    Modul={0}  Struktur=0x{1:X}  Hint=0x{2:X}  Name={3}" -f $h.Module, $h.StructOff, $h.Hint, $OLD)
            if (-not $Apply) {
                Write-Host "    [trockener Lauf] - mit -Apply patchen"
                continue
            }
            if ($Backup -and -not (Test-Path "$path.pre-abifix.orig")) {
                Write-Host "    Sicherung: $path.pre-abifix.orig"
                $fs.Close()
                Copy-Item $path "$path.pre-abifix.orig"
                $fs = [IO.File]::Open($path, 'Open', 'ReadWrite', 'ReadWrite')
            }
            $fs.Seek($abs, 'Begin') | Out-Null; $fs.WriteByte(0x49)
            $fs.Seek($h.StructOff, 'Begin') | Out-Null; $fs.WriteByte([byte]($NEW_HINT -band 0xFF)); $fs.WriteByte([byte](($NEW_HINT -shr 8) -band 0xFF))
            Write-Host ("    GEPATCHT: Q(0x51)->I(0x49) bei Datei-Offset 0x{0:X}; Hint 0x{1:X}->0x{2:X}; neuer Name={3}" -f $abs, $h.Hint, $NEW_HINT, (StrAt $fs ($h.StructOff + 2) 40))
        }
    } finally { $fs.Close() }
}

$targets = @("$QtDir\bin\Qt6WebEngineCore.dll") + $Files | Where-Object { $_ }
if (-not $Apply) { Write-Host "Trockener Lauf (kein -Apply):" }
foreach ($t in $targets) { Fix-Core $t }
Write-Host "fertig."
