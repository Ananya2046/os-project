# Running Mini Linux Shell on Windows

> **Course**: OS Lab (24B11CS312), JIIT Noida  
> **Team**: Tanishq Pundir, Anvi, Annanya Aggarwal, Palak Chauhan

---

## Why WSL?

`mysh` is a **POSIX shell** that uses Linux/Unix system calls (`fork`, `execvp`, `pipe`, `setpgid`, `tcsetpgrp`, `sigaction`, etc.) which do **not** exist natively on Windows. The Windows Subsystem for Linux (WSL) provides a full Linux kernel that runs these calls natively — the shell runs at near-native speed with zero code changes.

---

## Quick Start (3 Steps)

### Step 1: Install WSL2

Open **PowerShell as Administrator** and run:

```powershell
wsl --install
```

This installs WSL2 with Ubuntu. **Restart your computer** when prompted.

After restart, Ubuntu will launch automatically — create a username and password when asked.

### Step 2: Install Dependencies (inside WSL/Ubuntu)

Open the Ubuntu terminal (from Start menu) and run:

```bash
sudo apt update
sudo apt install -y build-essential python3 python3-tk
```

### Step 3: Build and Run

**Option A — Use the Windows launcher script:**

Double-click `run_windows.bat` in the project folder.  
Or right-click `run_windows.ps1` → "Run with PowerShell".

**Option B — Run manually in WSL terminal:**

```bash
# Navigate to the project directory
# If your project is at C:\Users\YourName\Desktop\os lab project\mini-shell
cd /mnt/c/Users/YourName/Desktop/"os lab project"/mini-shell

# Build the shell
make

# Run the shell in terminal mode
./mysh

# Or launch the GUI
python3 gui_app.py
```

---

## GUI on Windows

### Windows 11 (WSLg — built-in, nothing extra needed)

Windows 11's WSL includes **WSLg**, which provides native GUI support. The Tkinter window opens automatically — no extra setup needed.

If the GUI doesn't appear, update WSL:

```powershell
wsl --update
```

### Windows 10 (needs X server)

Windows 10 does not include WSLg. You need an X server:

1. **Download and install VcXsrv**: https://sourceforge.net/projects/vcxsrv/
2. **Launch XLaunch** with these settings:
   - Display number: `0`
   - Start no client
   - Check **"Disable access control"**
3. **In WSL terminal**, set the display variable:

```bash
export DISPLAY=:0
# Then launch the GUI:
python3 gui_app.py
```

> **Tip**: Add `export DISPLAY=:0` to your `~/.bashrc` so it persists.

---

## Running the Automated Tests on Windows

```bash
# In WSL terminal:
cd /mnt/c/Users/YourName/Desktop/"os lab project"/mini-shell
make test
```

All 22 tests should pass identically to Linux/macOS.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `wsl: command not found` | Install WSL: open admin PowerShell → `wsl --install` → restart |
| `gcc: command not found` | In WSL: `sudo apt install build-essential` |
| GUI window doesn't appear (Win 11) | Run `wsl --update` in PowerShell |
| GUI window doesn't appear (Win 10) | Install VcXsrv and set `export DISPLAY=:0` |
| `python3: No module named tkinter` | In WSL: `sudo apt install python3-tk` |
| Permission denied on `./mysh` | In WSL: `chmod +x mysh` |
| Path not found in WSL | Use `/mnt/c/...` path format for Windows drives |
| Slow file access | Copy project to WSL filesystem: `cp -r /mnt/c/.../mini-shell ~/mini-shell` |

---

## Architecture on Windows

```
┌──────────────────────────────────────────────────┐
│  Windows Desktop                                  │
│  ┌─────────────────────────────────────────────┐  │
│  │  Python Tkinter GUI (gui_app.py)            │  │
│  │  Native Windows desktop window via WSLg     │  │
│  └──────────────┬──────────────────────────────┘  │
│                 │ pty.fork() / subprocess          │
│  ┌──────────────┴──────────────────────────────┐  │
│  │  WSL2 Linux Kernel                          │  │
│  │  ┌────────────────────────────────────────┐  │  │
│  │  │  ./mysh (C binary)                     │  │  │
│  │  │  fork, execvp, pipe, setpgid,          │  │  │
│  │  │  tcsetpgrp, sigaction, waitpid         │  │  │
│  │  └────────────────────────────────────────┘  │  │
│  │  Named Pipe (FIFO) for GUI events →         │  │
│  └──────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────┘
```

---

## File Summary

| File | Purpose |
|------|---------|
| `run_windows.bat` | Double-click launcher for Windows (CMD) |
| `run_windows.ps1` | PowerShell launcher for Windows |
| `gui_app.py` | Cross-platform GUI (auto-detects Windows/WSL) |
| `Makefile` | Builds `mysh` (run inside WSL on Windows) |
| `docs/WINDOWS_SETUP.md` | This file — Windows setup guide |
