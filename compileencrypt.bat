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
echo Compiling Windows resource file (Admin Manifest)...
rc.exe resource.rc
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Resource compilation failed.
    pause
    exit /b 1
)

echo.
echo Compiling encrypt.exe...
cl.exe /EHsc /W4 /O2 /DUNICODE /D_UNICODE encrypt.cpp resource.res /Fe:encrypt.exe /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib advapi32.lib ole32.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation Complete! Executable created: encrypt.exe
) else (
    echo.
    echo Compilation Failed!
)

pause