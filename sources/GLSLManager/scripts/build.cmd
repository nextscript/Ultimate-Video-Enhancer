@echo off
rem Baut Plugin und Tests mit der Toolchain des Players
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;M:\LAPS\Qt\6.12.0\msvc2022_64\bin;%PATH%"
cd /d "%~dp0.."
if not exist build\build.ninja (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=M:/LAPS/Qt/6.12.0/msvc2022_64 || exit /b 1
)
cmake --build build %* || exit /b 1
