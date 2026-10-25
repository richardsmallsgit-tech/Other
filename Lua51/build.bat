@echo off
setlocal EnableExtensions
cd /d "%~dp0"
if not exist "src\xenlib.c" (echo [ERROR] Run this from the lua5.1 folder.& exit /b 1)
set "ROOT=%CD%"
set "OUT=%ROOT%\build\windows"
if not exist "%OUT%" mkdir "%OUT%"
where cl.exe >nul 2>&1
if not errorlevel 1 goto :msvc
where gcc.exe >nul 2>&1
if not errorlevel 1 goto :gcc
echo [ERROR] No C compiler found. Install Visual Studio Build Tools with Desktop development with C++, or MinGW-w64 GCC.
echo [INFO] For MSVC, run this from the x64 Native Tools Command Prompt.
exit /b 1

:msvc
echo [INFO] Building XenLua with MSVC...
setlocal EnableDelayedExpansion
set "SOURCES="
for %%F in (src\*.c) do if /I not "%%~nxF"=="lua.c" if /I not "%%~nxF"=="luac.c" if /I not "%%~nxF"=="print.c" if /I not "%%~nxF"=="wmain.c" set "SOURCES=!SOURCES! %%F"
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_WARNINGS /I src /Fe:"%OUT%\xenlua.exe" /Fo:"%OUT%\\" src\lua.c !SOURCES! /link /INCREMENTAL:NO
if errorlevel 1 goto :fail
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_WARNINGS /I src /Fe:"%OUT%\xenluac.exe" /Fo:"%OUT%\\" src\luac.c src\print.c !SOURCES! /link /INCREMENTAL:NO
if errorlevel 1 goto :fail
goto :test

:gcc
echo [INFO] Building XenLua with GCC...
setlocal EnableDelayedExpansion
set "SOURCES="
for %%F in (src\*.c) do if /I not "%%~nxF"=="lua.c" if /I not "%%~nxF"=="luac.c" if /I not "%%~nxF"=="print.c" if /I not "%%~nxF"=="wmain.c" set "SOURCES=!SOURCES! %%F"
gcc -O2 -I src -o "%OUT%\xenlua.exe" src\lua.c !SOURCES! -lm
if errorlevel 1 goto :fail
gcc -O2 -I src -o "%OUT%\xenluac.exe" src\luac.c src\print.c !SOURCES! -lm
if errorlevel 1 goto :fail

:test
echo [INFO] Running XenLua tests...
"%OUT%\xenlua.exe" "%ROOT%\test_xen.lua"
if errorlevel 1 goto :fail
echo.
echo [SUCCESS] Windows build and tests passed.
echo [OUTPUT] %OUT%\xenlua.exe
echo [OUTPUT] %OUT%\xenluac.exe
exit /b 0
:fail
echo [ERROR] Build or tests failed.
exit /b 1
