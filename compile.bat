@echo off
setlocal enabledelayedexpansion

echo Finding Visual Studio installation...

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "!VSWHERE!" (
    echo Error: vswhere.exe not found.
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -version [16.0^,20.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo Error: Visual Studio C++ build tools not found.
    pause
    exit /b 1
)

call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"

echo.
echo Compiling BackgroundWatcher.exe...
cl.exe /EHsc /W4 /O2 /DUNICODE /D_UNICODE BackgroundWatcher.cpp /Fe:BackgroundWatcher.exe /link /SUBSYSTEM:WINDOWS user32.lib winhttp.lib shell32.lib ole32.lib

echo.
echo Compiling LockOverlay.exe...
cl.exe /EHsc /W4 /O2 /DUNICODE /D_UNICODE LockOverlay.cpp /Fe:LockOverlay.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib bcrypt.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation Complete!
) else (
    echo.
    echo Compilation Failed!
)

pause