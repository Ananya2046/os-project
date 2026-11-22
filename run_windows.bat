@echo off
REM Top-level Windows launcher forwarding to mini-shell\run_windows.bat
cd /d "%~dp0mini-shell"
call run_windows.bat
