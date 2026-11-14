@echo off
setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Error: vswhere.exe not found. Please install Visual Studio 2019 or 2022 with C++ Desktop tools.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALL=%%i"
)

if not defined VSINSTALL (
    echo Error: Visual Studio C++ x64 build tools not found.
    exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" > nul
if errorlevel 1 exit /b 1

echo Compiling resources...
rc.exe /nologo /fo src\app.res src\app.rc
if errorlevel 1 exit /b 1

echo Building hypertree.exe...
cl.exe /nologo /utf-8 /O2 /Oi /Ot /GL /std:c++20 /EHsc /DUNICODE /D_UNICODE /W3 /Fe:hypertree.exe src\main.cpp src\scanner.cpp src\treemap.cpp src\app.res /link /LTCG /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:NO /MANIFESTINPUT:src\app.manifest
set "ERR=%ERRORLEVEL%"

del /q *.obj src\app.res 2>nul
if not "%ERR%"=="0" exit /b %ERR%

echo Build succeeded: hypertree.exe
