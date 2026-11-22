# Mini Linux Shell (`mysh`) with Process and Job Control

> **Course**: Operating Systems Lab (Course Code: 24B11CS312)  
> **Institution**: Jaypee Institute of Information Technology (JIIT), Noida  
> **Language**: C (C11 / POSIX `_POSIX_C_SOURCE 200809L`)  
> **Target OS**: Linux (Ubuntu 20.04 / 22.04 / 24.04 LTS), POSIX-compliant  

---

## 1. Project Overview

`mysh` is a robust, modular, interactive command-line interpreter (shell) and native desktop GUI written in C for Linux. Designed as an Operating Systems lab project (Course Code: 24B11CS312, JIIT Noida), it demonstrates the core systems programming concepts taught in undergraduate computer science:

**Project Team Members**:
- **Tanishq Pundir**
- **Anvi**
- **Annanya Aggarwal**
- **Palak Chauhan**

**Core Operating Systems Concepts Demonstrated**:
- **Process Creation & Management**: `fork()`, `execvp()`, `_exit()`, `waitpid()`
- **Process Groups & Sessions**: `setpgid()`, `getpgrp()`, `getpid()`
- **Terminal & Job Control**: `tcsetpgrp()`, `tcgetpgrp()`
- **Inter-Process Communication (IPC)**: UNIX unidirectional data channels via `pipe()` and Named Pipes (FIFOs)
- **File Descriptor Duplication & I/O Redirection**: `dup2()`, `open()`, `close()`
- **Asynchronous Signal Handling**: `sigaction()`, `sigprocmask()`, `sigsuspend()`
- **Native Desktop GUI**: Built using GTK 3 & VTE (C) and Tkinter (Python) over pseudo-terminals (`pty`) with zero web/localhost dependencies.

---

## 2. Directory Layout

```
mini-shell/
├── Makefile                # Build system (all, clean, test, valgrind)
├── README.md               # User manual and quick-start guide
├── include/                # Header definitions with modular guards
│   ├── builtins.h          # Built-in command interfaces (cd, pwd, fg, bg, jobs, etc.)
│   ├── executor.h          # Process execution and pipeline orchestration
│   ├── jobs.h              # Job control table data structures and operations
│   ├── parser.h            # Lexer and AST structures for pipeline commands
│   ├── signals.h           # Signal handling and terminal pgrp management
│   └── utils.h             # Memory allocation and string utility helpers
├── src/                    # Source implementations (< 50 lines per function)
│   ├── builtins.c          # cd, pwd, exit, jobs, fg, bg, export, history, help
│   ├── executor.c          # fork, execvp, dup2, pipe creation, sigsuspend waiting
│   ├── jobs.c              # Job table, states (RUNNING, STOPPED, DONE), +, - tracking
│   ├── main.c              # REPL prompt loop, asynchronous reaping, semicolon chaining
│   ├── parser.c            # Tokenizer, quoting, environment expansion, wildcards (glob)
│   ├── signals.c           # sigaction for SIGINT/SIGTSTP/SIGCHLD, process groups
│   └── utils.c             # safe_malloc, safe_realloc, safe_strdup, trim_whitespace
├── tests/                  # Automated verification test suite
│   └── run_tests.sh        # 22-test automated script covering all 9 project categories
└── docs/                   # Academic documentation and lab submission materials
    ├── ARCHITECTURE.md     # System architecture diagrams (ASCII + Mermaid)
    ├── REPORT.md           # Full OS Lab technical report
    └── VIVA_NOTES.md       # 20 key viva examination questions and answers
```

---

## 3. Features Supported

### 3.1 Command Execution & Quoting
- Execution of all standard external Linux binaries (`ls`, `cat`, `grep`, `sleep`, etc.) via `execvp`.
- Exact word parsing preserving single quotes (`'hello world'`) and double quotes (`"spaces   inside"`).
- Semicolon command separator (`;`) for sequential multi-command execution.

### 3.2 Built-in Commands (Executed directly in parent process)
- `cd [dir]` - Changes current working directory.
  - `cd` with no arguments navigates to `$HOME`.
  - `cd -` navigates to `$OLDPWD` and prints the directory path.
  - Automatically updates `PWD` and `OLDPWD` environment variables.
- `pwd` - Prints the absolute current working directory.
- `export VAR=VALUE` - Sets environment variables in the parent shell environment.
- `jobs` - Lists active running and stopped jobs with current (`+`) and previous (`-`) markers.
- `fg [%job_id]` - Transfers terminal ownership and resumes a job in the foreground.
- `bg [%job_id]` - Resumes a stopped job in the background (sends `SIGCONT`).
- `history` - Displays numbered command history for the session.
- `help` - Displays summary of built-in commands and features.
- `exit [code]` - Cleanly terminates the shell.
  - Warns if stopped jobs exist: `"There are stopped jobs."`.
  - If repeated consecutively, sends `SIGHUP` and `SIGCONT` to stopped jobs and exits.

### 3.3 I/O Redirection
- Standard Input Redirection: `< filename`
- Standard Output Redirection (Truncate): `> filename` (mode `0644`)
- Standard Output Redirection (Append): `>> filename` (mode `0644`)
- Standard Error Redirection: `2> filename` (mode `0644`)
- Combined Redirections: `sort < input.txt > output.txt`
- I/O redirection also works with parent built-in commands (e.g. `pwd > out.txt`, `help > help.txt`).

### 3.4 Multi-Stage Pipelines (IPC)
- Supports arbitrary N-stage pipelines: `cmd1 | cmd2 | cmd3 | ... | cmdN`
- Example: `cat fruits.txt | grep "^a" | tr a-z A-Z | sort -r > output.txt`
- All unused pipe file descriptors are cleanly closed in both child processes and the parent shell to prevent pipe deadlocks and file descriptor exhaustion.

### 3.5 Job Control & Process Groups
- Trailing ampersand (`&`) runs commands in the background and displays `[job_id] pgid`.
- Every pipeline executes in its own unique process group (`setpgid`), with the first process as group leader.
- `setpgid` is called in **both child and parent** to prevent race conditions.
- Terminal ownership is handed over to foreground jobs with `tcsetpgrp()` and reclaimed upon job stop or completion.
- Interactive keyboard signals:
  - `Ctrl+C` (`SIGINT`): Terminates only the foreground job; never terminates the shell. At an empty prompt, restarts the prompt on a new line.
  - `Ctrl+Z` (`SIGTSTP`): Suspends the foreground job, places it in `STOPPED` state, and returns to the shell prompt.
- Asynchronous `SIGCHLD` handler reaps child processes without blocking; completed background jobs are reported before the next prompt (`[1]+  Done  cmd &`).
- **Zero zombie processes**: all terminated children are systematically reaped.

### 3.6 Bonus Extensions
- Variable Expansion: `$VAR`, `$?` (last exit code), `$$` (shell PID), `$HOME`, `$USER`.
- Wildcard Globbing: Pathname pattern matching (`*.c`, `*.h`, `*`) using `glob()`.

---

## 4. Build and Run Instructions

### 4.1 Prerequisites
On Ubuntu Linux / Debian:
```bash
sudo apt-get update
sudo apt-get install -y gcc make valgrind libgtk-3-dev libvte-2.91-dev
```

### 4.2 Compilation & Build Targets
```bash
cd mini-shell

# 1. Build terminal binary (./mysh)
make

# 2. Build native GTK 3 desktop GUI (./mysh-gui)
make gui

# 3. Run automated 22-test suite
make test

# 4. Run Valgrind / AddressSanitizer audit
make valgrind
```

### 4.3 Running the Native Desktop GUI
Launch the native desktop window (no web, no browser, no localhost):
```bash
# Launch GTK 3 native application:
./mysh-gui

# Or launch cross-platform native desktop window:
python3 gui_app.py
```

### 4.3 Running the Shell
Launch interactive session:
```bash
./mysh
```
Sample interaction:
```
mysh> echo "Welcome to mysh"
Welcome to mysh
mysh> cd /tmp ; pwd
/tmp
mysh> cd -
/Users/ananyaaggarwal/Desktop/os lab project/mini-shell
mysh> sleep 5 &
[1] 23145
mysh> jobs
[1]+  Running              sleep 5 &
mysh> sleep 6
[1]+  Done                 sleep 5 &
mysh> exit
```

### 4.4 Automated Testing
Run the complete automated test suite (22 tests covering all 9 requirement categories):
```bash
make test
```

### 4.5 Memory Leak Audit
Audit memory safety with Valgrind (or AddressSanitizer):
```bash
make valgrind
```

### 4.6 Clean Build Artifacts
```bash
make clean
```

---

## 5. Running on Windows (WSL)

`mysh` uses POSIX system calls (`fork`, `execvp`, `pipe`, `setpgid`, `tcsetpgrp`, `sigaction`) that require a Linux/Unix kernel. On Windows, the **Windows Subsystem for Linux (WSL2)** provides a full Linux kernel to run the shell natively.

### Quick Start

1. **Install WSL2** (Administrator PowerShell):
   ```powershell
   wsl --install
   ```
   Restart your computer when prompted.

2. **Install dependencies** (in WSL/Ubuntu terminal):
   ```bash
   sudo apt update && sudo apt install -y build-essential python3 python3-tk
   ```

3. **Build and Run**:
   - **Easy way**: Double-click `run_windows.bat` in the project folder.
   - **Manual way** (in WSL terminal):
     ```bash
     cd /mnt/c/Users/YourName/Desktop/"os lab project"/mini-shell
     make
     ./mysh          # Terminal mode
     python3 gui_app.py  # GUI mode
     ```

> **Windows 11**: GUI works out of the box via WSLg.  
> **Windows 10**: Requires an X server (e.g., VcXsrv). See [`docs/WINDOWS_SETUP.md`](docs/WINDOWS_SETUP.md) for details.

For the complete Windows setup guide, see: **[docs/WINDOWS_SETUP.md](docs/WINDOWS_SETUP.md)**

---

## 6. Technical Limitations
1. **Interactive Line Editing**: Advanced readline shortcuts (arrow keys, tab completion) are not included to adhere strictly to libc without external libraries (e.g., `libreadline`). Standard terminal canonical editing (Backspace, Enter, Ctrl+U, Ctrl+D) is supported natively by the tty driver.
2. **Compound Control Flow**: Shell scripting language constructs (`if/then`, `for/while`, shell functions) are out of scope for this process and job control lab.

