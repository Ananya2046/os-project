# Native Desktop GUI Testing Checklist (`mysh-gui` / `gui_app.py`)

> **Course**: Operating Systems Lab (24B11CS312), JIIT Noida  
> **Topic**: Quality Assurance & Verification Checklist for Native Desktop Shell GUI  

---

## 1. Zero Network / Zero Socket Verification (Hard Rule 10.1)
The native GUI communicates with `./mysh` strictly via local OS primitives:
- Pseudo-Terminal (`/dev/pty`)
- Named Pipe / FIFO (`/tmp/mysh_gui_fifo_*`)

### Port Audit Verification:
Run the following network audit while the GUI is running:
```bash
# On Linux:
ss -tulpn | grep -E "mysh|python|gui"
# On macOS:
netstat -an | grep LISTEN | grep -E "8080|mysh"
```
**Expected Result**: Empty output. **Zero open TCP/UDP ports or sockets.**

---

## 2. Interactive Feature Verification Checklist

| # | Test Action | Verification Step | Expected Status |
|---|---|---|:---:|
| 1 | **Window Launch** | Launch `./mysh-gui` or `python3 gui_app.py` | Native OS desktop window opens with embedded terminal, Jobs panel, Pipeline visualizer, and Signal log |
| 2 | **Embedded Terminal Input** | Type `pwd` and press Enter | Current working directory is printed in terminal |
| 3 | **I/O Redirection** | Type `echo "testing" > test.txt ; cat < test.txt` | Output printed, file created and read via file descriptors |
| 4 | **3-Stage Pipeline** | Click "🔗 Pipeline Demo" or type `ls src \| grep .c \| wc -l` | Pipeline visualizer dynamically draws 3 connected stages with arrows and PIDs |
| 5 | **Background Job (&)** | Type `sleep 20 &` | Jobs Table immediately populates `[1] Running` in **Green**; Signal log records `JOB_ADD` and `PROC_NEW` |
| 6 | **Job Stop (SIGTSTP)** | Type `sleep 30`, then press `Ctrl+Z` (or select job and click `⏸ Stop`) | Process suspends; Jobs Table marks state `STOPPED` in **Amber/Orange**; Signal log notes `SIGTSTP` |
| 7 | **Background Resume (bg)** | Select stopped job and click `▶ bg (SIGCONT)` | Shell sends `SIGCONT` to process group; Job status transitions to `RUNNING` (Green) |
| 8 | **Foreground Resume (fg)** | Select job and click `⬆ fg` | Terminal reclaims job in foreground via `tcsetpgrp()` |
| 9 | **Kill Button (SIGKILL)** | Select running job and click `✖ Kill (SIGKILL)` | Process group terminated immediately with `kill(-pgid, SIGKILL)`; row removed from table |
| 10| **Run Demo Mode** | Click `▶ Run Demo` on toolbar | Automated sequence types commands with pauses (redirection, 3-stage pipe, background job, jobs listing) |
| 11| **About Dialog** | Click `ℹ️ Team & About` on toolbar | Displays team names (**Tanishq Pundir, Anvi, Annanya Aggarwal, Palak Chauhan**) and project details |
| 12| **Clean Window Close** | Close the GUI window via `X` button | Shell process is terminated via `SIGHUP/SIGCONT`; FIFO in `/tmp` is deleted; zero zombie/orphan processes remain |

---

## 3. Process Table & Zombie Cleanliness Audit

After closing the GUI window:
```bash
ps -ef | grep -E "mysh|defunct" | grep -v grep
```
**Expected Result**: Clean process table. Zero lingering `<defunct>` zombie processes or orphan child processes.

