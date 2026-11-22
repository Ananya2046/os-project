# ===================================================================
#  Mini Linux Shell (mysh) - Windows PowerShell Launcher
#  Course: OS Lab (24B11CS312), JIIT Noida
#
#  Usage: Right-click -> "Run with PowerShell"
#         Or: powershell -ExecutionPolicy Bypass -File run_windows.ps1
# ===================================================================

Write-Host "===================================================" -ForegroundColor Cyan
Write-Host "  Mini Linux Shell (mysh) - Windows Launcher" -ForegroundColor Cyan
Write-Host "  OS Lab (24B11CS312), JIIT Noida" -ForegroundColor Cyan
Write-Host "===================================================" -ForegroundColor Cyan
Write-Host ""

# Check if WSL is available
try {
    $wslStatus = wsl --status 2>&1
    Write-Host "[OK] WSL detected." -ForegroundColor Green
} catch {
    Write-Host "ERROR: WSL is not installed on this system." -ForegroundColor Red
    Write-Host ""
    Write-Host "To install WSL, run in an Administrator PowerShell:" -ForegroundColor Yellow
    Write-Host "  wsl --install" -ForegroundColor White
    Write-Host ""
    Write-Host "After installation, restart your computer and run this script again."
    Read-Host "Press Enter to exit"
    exit 1
}

# Get the script directory and convert to WSL path
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$wslDir = (wsl wslpath -a $scriptDir).Trim()

Write-Host "[OK] Project directory (WSL): $wslDir" -ForegroundColor Green
Write-Host ""

# Check prerequisites in WSL
Write-Host "Checking prerequisites..." -ForegroundColor Yellow
$gccCheck = wsl bash -c "command -v gcc 2>/dev/null"
if (-not $gccCheck) {
    Write-Host "Installing build-essential in WSL..." -ForegroundColor Yellow
    wsl sudo apt-get update -qq
    wsl sudo apt-get install -y -qq build-essential
}

$tkCheck = wsl bash -c "python3 -c 'import tkinter' 2>/dev/null; echo `$?"
if ($tkCheck -ne "0") {
    Write-Host "Installing python3-tk in WSL..." -ForegroundColor Yellow
    wsl sudo apt-get update -qq
    wsl sudo apt-get install -y -qq python3 python3-tk
}

# Build
Write-Host ""
Write-Host "Building mysh..." -ForegroundColor Yellow
wsl bash -c "cd '$wslDir' && make"
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed." -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}
Write-Host "[OK] Build successful." -ForegroundColor Green

# Run automated tests
Write-Host ""
Write-Host "Running automated tests..." -ForegroundColor Yellow
wsl bash -c "cd '$wslDir' && make test"
Write-Host ""

# Launch GUI
Write-Host "Launching GUI..." -ForegroundColor Green
Write-Host "(Close the GUI window to return to this terminal.)" -ForegroundColor Gray
Write-Host ""

wsl bash -c "cd '$wslDir' && python3 gui_app.py"

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "If the GUI failed to open, you may need a display server:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Option A - Windows 11 with WSLg (recommended):" -ForegroundColor White
    Write-Host "  WSLg is built-in. Update WSL: wsl --update"
    Write-Host ""
    Write-Host "Option B - Windows 10 with VcXsrv:" -ForegroundColor White
    Write-Host "  1. Install VcXsrv from https://sourceforge.net/projects/vcxsrv/"
    Write-Host "  2. Launch XLaunch with 'Disable access control' checked"
    Write-Host "  3. In WSL run: export DISPLAY=:0"
    Write-Host "  4. Re-run this script"
}

Write-Host ""
Read-Host "Press Enter to exit"
