# UVE Player – Release bauen: Build, Staging (windeployqt), Installer, portable EXE, portable ZIP, Prüfsummen
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1
param(
    [string]$QtDir = $(if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { "M:\LAPS\Qt\6.12.0\msvc2022_64" }),
    [string]$Makensis = "",
    [string]$Generator = "Visual Studio 18 2026",
    [switch]$SkipTests
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(UVEPlayer VERSION ([0-9.]+)').Matches[0].Groups[1].Value
Write-Host "UVE Player $version"

if (-not $Makensis) {
    $Makensis = Get-ChildItem "$env:LOCALAPPDATA\electron-builder\Cache" -Recurse -Filter makensis.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.DirectoryName -notlike "*\Bin" } | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Makensis) { $Makensis = "${env:ProgramFiles(x86)}\NSIS\makensis.exe" }   # Standardinstallation (z. B. GitHub Actions)
if (-not (Test-Path $Makensis)) { throw "makensis.exe nicht gefunden (Parameter -Makensis)" }

# 1) Build + Tests
cmake -S . -B build -G $Generator -A x64 -DCMAKE_PREFIX_PATH="$QtDir" | Out-Null
cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Build fehlgeschlagen" }
if (-not $SkipTests) {
    ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests fehlgeschlagen – kein Release" }
}

# 2) Staging
$stage = Join-Path $root "release\stage"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage, "$stage\mpv", "$stage\tools\yt-dlp", "$stage\plugins\uve", "$stage\docs" | Out-Null
Copy-Item build\bin\UVEPlayer.exe $stage
Copy-Item build\bin\UVEThumbnailer.dll $stage   # Explorer-Vorschaubilder (Shell-Erweiterung)
Copy-Item deps\mpv\libmpv-2.dll, deps\mpv\version.json "$stage\mpv"
Copy-Item deps\tools\yt-dlp\yt-dlp.exe, deps\tools\yt-dlp\version.json "$stage\tools\yt-dlp"
Copy-Item deps\tools\7zr.exe "$stage\tools"
Copy-Item build\bin\plugins\*.dll "$stage\plugins"
Copy-Item plugins\uve\data\* "$stage\plugins\uve" -Recurse
New-Item -ItemType Directory -Force "$stage\lang" | Out-Null   # Sprachen: kompilierte .qm + Quellen (.ts) + Anleitung
Copy-Item build\bin\lang\*.qm "$stage\lang"
Copy-Item lang\*.ts, lang\README.md "$stage\lang"
if ((Get-ChildItem "$stage\lang\*.qm").Count -lt 6) { throw "Sprachdateien (.qm) fehlen im Staging" }
Copy-Item app\resources\icon.ico $stage
Copy-Item docs\*.md "$stage\docs"
Copy-Item docs\THIRD_PARTY_LICENSES.md "$stage\docs\THIRD_PARTY_LICENSES.txt"
Copy-Item README.md, CHANGELOG.md $stage
& "$QtDir\bin\windeployqt.exe" --release --qmldir app\qml --qmldir plugins\uve\qml --qmldir plugins\examples\sleeptimer `
    --no-translations --no-system-d3d-compiler --no-opengl-sw --dir $stage "$stage\UVEPlayer.exe" | Out-Null
# Plugin-DLLs können weitere Qt-Module nutzen (UVE: Network, Concurrent) → ebenfalls auswerten
& "$QtDir\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --no-quick-import `
    --dir $stage (Get-ChildItem "$stage\plugins\*.dll").FullName | Out-Null
foreach ($m in "Qt6Network.dll", "Qt6Concurrent.dll") {
    if (-not (Test-Path "$stage\$m")) { throw "Qt-Modul fehlt im Staging: $m" }
}
# Visual-C++-Laufzeit app-lokal beilegen (läuft dann auch ohne installiertes VC++-Redistributable)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -prerelease -products * -property installationPath
$crt = Get-ChildItem "$vsPath\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory | Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw "VC++-Laufzeit (Microsoft.VC*.CRT) nicht gefunden" }
Copy-Item "$crt\*.dll" $stage -Force
foreach ($m in "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll") {
    if (-not (Test-Path "$stage\$m")) { throw "VC++-Laufzeit fehlt im Staging: $m" }
}

# 3) Installer und portable EXE
$out = Join-Path $root "release"
& $Makensis /V2 "/DVERSION=$version" "/DSTAGE=$stage" "/DOUTFILE=$out\UVE-Player-$version-setup.exe" packaging\installer.nsi
if ($LASTEXITCODE -ne 0) { throw "Installer fehlgeschlagen" }
& $Makensis /V2 "/DVERSION=$version" "/DSTAGE=$stage" "/DOUTFILE=$out\UVE-Player-$version-portable.exe" packaging\portable.nsi
if ($LASTEXITCODE -ne 0) { throw "Portable EXE fehlgeschlagen" }

# 4) Portable ZIP (Ordner mit portable.txt → Daten in .\data)
$zipDir = Join-Path $out "UVE-Player-$version-portable"
Remove-Item $zipDir -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item $stage $zipDir -Recurse
Set-Content "$zipDir\portable.txt" "Portable mode: settings and data are stored in the 'data' subfolder next to UVEPlayer.exe." -Encoding UTF8
Compress-Archive -Path "$zipDir\*" -DestinationPath "$out\UVE-Player-$version-portable.zip" -Force
Remove-Item $zipDir -Recurse -Force

# 5) Prüfsummen
$files = Get-ChildItem $out -File | Where-Object { $_.Name -like "UVE-Player-$version-*" }
$files | ForEach-Object { "{0}  {1}" -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name } |
    Set-Content "$out\SHA256SUMS.txt" -Encoding ASCII
$files | Select-Object Name, @{n = 'MB'; e = { [math]::Round($_.Length / 1MB, 1) } } | Format-Table -AutoSize
