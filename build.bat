@echo off
setlocal
title CS2-Lan-ESP Build
color 0A

echo.
echo  ===============================================
echo   CS2-Lan-ESP Build
echo  ===============================================
echo.

cd /d "%~dp0"

if not exist "build" (
    echo  [*] Creating build directory...
    mkdir build
)

cd build

echo  [*] Running CMake configure...
cmake .. -A x64
if %errorlevel% neq 0 (
    echo  [X] CMake configure failed.
    pause
    exit /b 1
)

echo.
echo  [*] Building Release...
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo  [X] Build failed.
    pause
    exit /b 1
)

echo.
echo  ===============================================
echo   Build Complete!
echo  ===============================================
echo.
echo   Output files:
echo     build\Release\USB-Lan-ESP.exe
echo     build\Release\USB-Lan-Monitor.exe
echo.
pause
