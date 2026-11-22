@echo off
REM ===================================================================
REM  Mini Linux Shell (mysh) - Windows Launcher
REM  Course: OS Lab (24B11CS312), JIIT Noida
REM
REM  This script launches the mysh GUI on Windows via WSL.
REM  Prerequisites:
REM    1. WSL2 installed: wsl --install
REM    2. Ubuntu (or any Linux distro) installed in WSL
REM    3. Inside WSL: sudo apt install build-essential python3 python3-tk
REM ===================================================================

echo ===================================================
echo   Mini Linux Shell (mysh) - Windows Launcher
echo   OS Lab (24B11CS312), JIIT Noida
echo ===================================================
echo.

REM Check if WSL is available
wsl --status >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: WSL is not installed on this system.
    echo.
    echo To install WSL, run the following in an Administrator PowerShell:
    echo   wsl --install
    echo.
    echo After installation, restart your computer and run this script again.
    echo.
    pause
    exit /b 1
)

echo [OK] WSL detected.
echo.

REM Check if build-essential is available
wsl bash -c "command -v gcc >/dev/null 2>&1"
if %ERRORLEVEL% neq 0 (
    echo WARNING: GCC not found in WSL. Installing build-essential...
    wsl sudo apt-get update -qq && wsl sudo apt-get install -y -qq build-essential
)

REM Check if python3-tk is available
wsl bash -c "python3 -c 'import tkinter' 2>/dev/null"
if %ERRORLEVEL% neq 0 (
    echo WARNING: Python3-tk not found in WSL. Installing...
    wsl sudo apt-get update -qq && wsl sudo apt-get install -y -qq python3 python3-tk
)

REM Convert current directory to WSL path
for /f "tokens=*" %%a in ('wsl wslpath -a "%~dp0"') do set WSL_DIR=%%a

echo [OK] Project directory (WSL): %WSL_DIR%
echo.
echo Building mysh...
wsl bash -c "cd '%WSL_DIR%' && make"
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed. Check error messages above.
    pause
    exit /b 1
)
echo [OK] Build successful.
echo.

echo Launching GUI...
echo (The GUI window will open shortly. Close it to exit.)
echo.

REM Launch the Python GUI
REM WSLg (Windows 11) provides native GUI support.
REM On Windows 10, users need an X server like VcXsrv.
wsl bash -c "cd '%WSL_DIR%' && python3 gui_app.py"

if %ERRORLEVEL% neq 0 (
    echo.
    echo If the GUI failed to open, you may need a display server:
    echo.
    echo Option A - Windows 11 with WSLg (recommended):
    echo   WSLg is built-in. Just update WSL: wsl --update
    echo.
    echo Option B - Windows 10 with VcXsrv:
    echo   1. Install VcXsrv from https://sourceforge.net/projects/vcxsrv/
    echo   2. Launch XLaunch with 'Disable access control' checked
    echo   3. In WSL, run: export DISPLAY=:0
    echo   4. Re-run this script
    echo.
)

pause
