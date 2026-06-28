@echo off
setlocal

set "VSPATH="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set "VSPATH=%%i"
)
if defined VSPATH (
    call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else (
    for /d %%d in ("C:\Program Files\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars64.bat") do (
        call "%%d" >nul 2>&1
        goto :found
    )
    echo ERROR: Cannot find VS. Run from Developer Command Prompt.
    exit /b 1
)
:found

echo === Building Bandizip-Patcher.exe ===
rc /nologo /fo resource.res resource.rc
if %errorlevel% neq 0 ( exit /b 1 )

cl /nologo /O2 /MT /W3 /GS- /utf-8 /DNDEBUG ^
    patcher.c resource.res /Fe:Bandizip-Patcher.exe ^
    /link /NOLOGO /SUBSYSTEM:WINDOWS,6.00 /MANIFEST:NO ^
    user32.lib kernel32.lib comctl32.lib gdi32.lib shell32.lib ole32.lib

if %errorlevel% neq 0 ( echo === FAILED === && exit /b 1 )

mt.exe -nologo -manifest patcher.manifest -outputresource:Bandizip-Patcher.exe;1
if %errorlevel% neq 0 ( echo Manifest embedding FAILED && exit /b 1 )

echo.
echo === SUCCESS ===
echo UAC Administrator manifest embedded.
del resource.res patcher.obj 2>nul
endlocal
