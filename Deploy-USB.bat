@echo off
setlocal enabledelayedexpansion
title CS2-Lan-ESP USB Deployment
color 0A

echo.
echo  ===============================================
echo   CS2-Lan-ESP USB Deployment Tool
echo  ===============================================
echo.

:: Check for build files (try Release subdir first, then build root)
set "BUILD_DIR=%~dp0build\Release"
if not exist "%BUILD_DIR%\USB-Lan-ESP.exe" set "BUILD_DIR=%~dp0build"
if not exist "%BUILD_DIR%\USB-Lan-ESP.exe" (
    echo  [!] Build files not found. Building project...
    echo.
    cd /d "%~dp0"
    if not exist "build" mkdir build
    cd build
    cmake .. >nul 2>&1
    cmake --build . --config Release
    if !errorlevel! neq 0 (
        echo  [X] Build failed. Install Visual Studio and CMake.
        pause
        exit /b 1
    )
    echo  [+] Build complete.
    echo.
)

:: List removable drives
echo  Available USB drives:
echo  ----------------------
set "DRIVE_COUNT=0"
for /f "tokens=1,2,*" %%a in ('wmic logicaldisk where "drivetype=2" get deviceid^,volumename^,size 2^>nul ^| findstr /r "^[A-Z]:"') do (
    set /a DRIVE_COUNT+=1
    set "DRIVE_!DRIVE_COUNT!=%%a"
    set "SIZE=%%c"
    if "!SIZE!"=="" set "SIZE=Unknown"
    echo   [!DRIVE_COUNT!] %%a %%b
)

if %DRIVE_COUNT%==0 (
    echo.
    echo  [X] No USB drives detected. Insert a USB drive and try again.
    pause
    exit /b 1
)

echo.
set /p "CHOICE=  Select drive number (1-%DRIVE_COUNT%): "

:: Validate choice
set "TARGET="
for /l %%i in (1,1,%DRIVE_COUNT%) do (
    if "%CHOICE%"=="%%i" set "TARGET=!DRIVE_%%i!"
)

if "%TARGET%"=="" (
    echo  [X] Invalid selection.
    pause
    exit /b 1
)

:: Remove trailing colon for path operations
set "TARGET_DRIVE=%TARGET%"
echo.
echo  [*] Deploying to %TARGET_DRIVE%...

:: Clean existing files (remove hidden/system first)
if exist "%TARGET_DRIVE%\.data" (
    attrib -h -s "%TARGET_DRIVE%\.data" >nul 2>&1
    attrib -h -s "%TARGET_DRIVE%\.data\*" >nul 2>&1
    rd /s /q "%TARGET_DRIVE%\.data" >nul 2>&1
)
if exist "%TARGET_DRIVE%\Setup.bat" del /f /q "%TARGET_DRIVE%\Setup.bat" >nul 2>&1

:: Create data folder
mkdir "%TARGET_DRIVE%\.data" >nul 2>&1

:: Copy files
echo  [*] Copying files...
copy /y "%BUILD_DIR%\USB-Lan-ESP.exe" "%TARGET_DRIVE%\.data\" >nul
copy /y "%BUILD_DIR%\USB-Lan-Monitor.exe" "%TARGET_DRIVE%\.data\" >nul
copy /y "%~dp0src\Core\NeacSafe\Driver.sys" "%TARGET_DRIVE%\.data\" >nul

:: Create config.ini
echo  [*] Creating config.ini...
(
echo [ESP]
echo enabled=1
echo box=1
echo health=1
echo bones=1
echo name=1
echo distance=1
echo head=1
echo visible_only=0
echo.
echo [Aimbot]
echo enabled=1
echo fov=5.0
echo smooth=3.0
echo bone=6
echo key=2
echo.
echo [RCS]
echo enabled=1
echo scale_x=1.0
echo scale_y=1.0
echo start_bullet=1
) > "%TARGET_DRIVE%\.data\config.ini"

:: Create Setup.bat
echo  [*] Creating launcher...
(
echo @echo off
echo setlocal enabledelayedexpansion
echo if not "%%1"=="go" ^(
echo     powershell -WindowStyle Hidden -Command "Start-Process cmd -ArgumentList '/c \"%%~f0\" go' -Verb RunAs -WindowStyle Hidden" 2^>nul
echo     exit /b
echo ^)
echo set "USB=%%~d0"
echo set "DATA=%%USB%%\.data"
echo set "TEMP_DIR=%%TEMP%%\svc_%%RANDOM%%%%RANDOM%%"
echo set "INSTALL=%%LOCALAPPDATA%%\Microsoft\Services"
echo if not exist "%%DATA%%\USB-Lan-Monitor.exe" exit /b
echo if not exist "%%INSTALL%%\w.vbs" ^(
echo     if not exist "%%INSTALL%%" mkdir "%%INSTALL%%" 2^>nul
echo     ^(
echo     echo Set f=CreateObject^^^("Scripting.FileSystemObject"^^^)
echo     echo Set s=CreateObject^^^("WScript.Shell"^^^)
echo     echo launched=""
echo     echo Do
echo     echo   found=False
echo     echo   For Each d In f.Drives
echo     echo     If d.IsReady And f.FolderExists^^^(d.DriveLetter^^^&":\.data"^^^) Then
echo     echo       If f.FileExists^^^(d.DriveLetter^^^&":\.data\USB-Lan-Monitor.exe"^^^) Then
echo     echo         found=True
echo     echo         If launched^^^<^^^>d.DriveLetter Then
echo     echo           s.Run """"^^^&d.DriveLetter^^^&":\Setup.bat"" go",0
echo     echo           launched=d.DriveLetter
echo     echo         End If
echo     echo         Exit For
echo     echo       End If
echo     echo     End If
echo     echo   Next
echo     echo   If Not found Then launched=""
echo     echo   WScript.Sleep 3000
echo     echo Loop
echo     ^) ^> "%%INSTALL%%\w.vbs"
echo     schtasks /create /tn "SvcHost" /tr "wscript.exe \"%%INSTALL%%\w.vbs\"" /sc onlogon /rl highest /f ^>nul 2^>^&1
echo ^)
echo mkdir "%%TEMP_DIR%%" 2^>nul
echo xcopy /y /h "%%DATA%%\USB-Lan-Monitor.exe" "%%TEMP_DIR%%\" ^>nul 2^>^&1
echo xcopy /y /h "%%DATA%%\USB-Lan-ESP.exe" "%%TEMP_DIR%%\" ^>nul 2^>^&1
echo xcopy /y /h "%%DATA%%\Driver.sys" "%%TEMP_DIR%%\" ^>nul 2^>^&1
echo xcopy /y /h "%%DATA%%\config.ini" "%%TEMP_DIR%%\" ^>nul 2^>^&1
echo start "" "%%TEMP_DIR%%\USB-Lan-Monitor.exe" "--tempdir=%%TEMP_DIR%%"
echo exit /b
) > "%TARGET_DRIVE%\Setup.bat"

:: Set hidden attributes
echo  [*] Setting hidden attributes...
attrib +h +s "%TARGET_DRIVE%\.data" >nul 2>&1
attrib +h +s "%TARGET_DRIVE%\.data\*" >nul 2>&1

:: Verify
echo.
echo  ===============================================
echo   Deployment Complete!
echo  ===============================================
echo.
echo   Target: %TARGET_DRIVE%
echo.
echo   Visible files:
dir /b "%TARGET_DRIVE%" 2>nul | findstr /v "^\."
echo.
echo   Hidden files (.data folder):
dir /b /a:h "%TARGET_DRIVE%\.data" 2>nul
echo.
echo  -----------------------------------------------
echo   USAGE:
echo   1. Plug USB into target system
echo   2. Run Setup.bat as Administrator (first time)
echo   3. USB will auto-launch on future plug-ins
echo   4. Press END to exit and clean up
echo  -----------------------------------------------
echo.
pause
