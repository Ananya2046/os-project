# Top-level PowerShell launcher forwarding to mini-shell\run_windows.ps1
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
& "$scriptDir\mini-shell\run_windows.ps1"
