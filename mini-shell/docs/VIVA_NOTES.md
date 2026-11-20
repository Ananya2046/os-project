# Operating Systems Lab Viva Examination Guide: Mini Linux Shell (`mysh`)

> **Course**: Operating Systems Lab (Course Code: 24B11CS312)  
> **Institution**: Jaypee Institute of Information Technology (JIIT), Noida  
> **Topic**: Comprehensive Viva Questions & Conceptual Answers for Shell Implementation  

---

### Q1. What is the fundamental difference between `fork()` and `execvp()`?
- **`fork()`**: Creates a brand new child process that is an exact duplicate of the calling parent process. It duplicates the address space (via Copy-On-Write), file descriptor tables, and environment, but assigns a unique Process ID (`PID`). `fork()` is called once and returns twice: returning `0` in the child and the child's `PID` in the parent.
- **`execvp()`**: Does not create a new process. Instead, it overlays and replaces the current process's text (code), data, heap, and stack segments with a new executable program binary from disk. It retains the same `PID` and open file descriptors (unless `FD_CLOEXEC` is set). It only returns if an error occurs (e.g. file not found, permission denied).

---

### Q2. Why MUST commands like `cd`, `pwd`, and `exit` be built-in rather than external binaries?
- When an external command runs, the shell calls `fork()` to create an isolated child process, executes the binary, and waits.
- If `cd` were an external binary (e.g., `/bin/cd`), the child process would change its own working directory via `chdir()`. When the child terminates, its process address space is destroyed, leaving the parent shell's current working directory completely unchanged.
- Therefore, commands that modify the shell's internal state—such as current directory (`cd`), environment variables (`export`), job tables (`jobs`, `fg`, `bg`), or process termination (`exit`)—**must execute directly inside the parent shell process** without forking.

---

### Q3. What is a zombie process (`<defunct>`), why does it occur, and how does `mysh` prevent zombies?
- **Definition**: A zombie process is a child process that has finished execution (called `exit()` or terminated by signal), but whose termination entry still remains in the OS process table.
- **Why it occurs**: The kernel retains the child's termination status code, CPU usage statistics, and PID until the parent process reads it via `wait()` or `waitpid()`.
- **How `mysh` prevents zombies**: `mysh` installs an asynchronous `SIGCHLD` signal handler. Whenever any background or foreground child terminates or stops, the kernel sends `SIGCHLD`. The handler executes:
  ```c
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
  ```
  The `WNOHANG` flag reaps all available terminated children immediately without blocking, purging them from the process table and ensuring zero zombies (`ps` shows zero `<defunct>`).

---

### Q4. What is an orphan process, and how does Linux handle orphaned children?
- **Definition**: An orphan process is an actively running child process whose parent terminates before the child finishes.
- **Kernel Handling**: The Linux kernel automatically re-parents orphaned processes to `init` (PID 1) or a modern user-space subreaper (`systemd`). The `init` process periodically calls `wait()` to reap orphaned children when they terminate, preventing them from turning into permanent zombies.

---

### Q5. How does a UNIX pipe (`pipe()`) work internally at the kernel level?
- A UNIX pipe is a unidirectional IPC (Inter-Process Communication) channel allocated in kernel memory as a circular FIFO buffer (typically 64 KB).
- `int pipe(int pipefd[2])` allocates two new file descriptors:
  - `pipefd[0]`: Open for **reading**.
  - `pipefd[1]`: Open for **writing**.
- Writing to `pipefd[1]` places bytes into the kernel buffer; reading from `pipefd[0]` consumes bytes in first-in, first-out order. If the buffer is full, write blocks; if the buffer is empty, read blocks until data arrives or all write descriptors are closed (signaling `EOF`).

---

### Q6. Why is it strictly necessary to close unused pipe descriptors in BOTH parent and child?
1. **Detecting End-Of-File (EOF)**: A reading process reading from a pipe will only receive an `EOF` (return value `0` from `read()`) after **every single file descriptor** referring to the write-end of that pipe across the entire operating system has been closed. If the parent shell (or a sibling child) forgets to close `pipefd[1]`, the reader will block indefinitely waiting for more data, hanging the pipeline (e.g. `cat file | grep foo` hangs).
2. **Preventing Resource Leaks**: Operating systems have a per-process limit on open file descriptors (`RLIMIT_NOFILE`). Failing to close unused descriptors leads to file descriptor exhaustion (`EMFILE`).

---

### Q7. What does `dup2(oldfd, newfd)` do, and how is it used for I/O redirection?
- `dup2(oldfd, newfd)` duplicates an open file descriptor `oldfd` onto `newfd`. If `newfd` is already open, `dup2` silently closes it first atomically.
- **Redirection Example**:
  To redirect standard output to `output.txt`:
  ```c
  int fd = open("output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  dup2(fd, STDOUT_FILENO); // STDOUT_FILENO (fd 1) now points to output.txt
  close(fd);               // Original fd closed; fd 1 remains open
  ```
  When `execvp()` runs, any library call writing to stdout (`printf`, `write(1, ...)`) writes directly to `output.txt`.

---

### Q8. What is a Process Group, and what is the role of the Process Group ID (PGID)?
- A **Process Group** is a collection of one or more related processes (such as all processes in a pipeline `ls | grep txt | wc -l`) that share a common Process Group ID (`PGID`).
- Process groups allow the terminal driver and operating system to broadcast signals (e.g., `SIGINT`, `SIGTSTP`, `SIGCONT`) to an entire pipeline simultaneously using `kill(-pgid, signal)`.

---

### Q9. Why must `setpgid(pid, pgid)` be called in BOTH the parent and the child?
- **The Scheduling Race Condition**:
  - If only the child calls `setpgid(pid, pgid)`, the operating system might schedule the parent first. The parent might attempt to call `tcsetpgrp(tty, pgid)` or `waitpid(-pgid, ...)` before the child has executed `setpgid()`, resulting in errors (`EACCES`, `ESRCH`) because the process group does not yet exist.
  - If only the parent calls `setpgid(pid, pgid)`, the child might run and execute `execvp()` before the parent's `setpgid()` executes, which POSIX forbids (`EACCES`).
- **Solution**: Calling `setpgid(pid, pgid)` in **both parent and child** guarantees that regardless of which process the CPU scheduler runs first, the child is guaranteed to be in its correct process group before either process performs any operation that depends on it.

---

### Q10. What does `tcsetpgrp(fd, pgrp)` do, and why is it essential for terminal job control?
- `tcsetpgrp(int fd, pid_t pgrp)` assigns terminal foreground ownership of the terminal associated with file descriptor `fd` to the process group `pgrp`.
- In UNIX, only the single **foreground process group** has access to read keyboard input from the terminal and receive terminal-generated signals (`Ctrl+C` -> `SIGINT`, `Ctrl+Z` -> `SIGTSTP`).
- Any background process group that attempts to read from the terminal is automatically suspended by the kernel with a `SIGTTIN` signal.

---

### Q11. Why does the shell ignore `SIGINT`, `SIGTSTP`, and `SIGQUIT`, while children restore `SIG_DFL`?
- When a user presses `Ctrl+C` or `Ctrl+Z`, the terminal driver generates `SIGINT` or `SIGTSTP` and delivers it to the current foreground process group.
- If the shell process did not ignore these signals, typing `Ctrl+C` would terminate the shell itself, kicking the user back to the operating system login.
- Therefore, the shell ignores these signals. Before invoking `execvp()`, every child process restores the default disposition (`SIG_DFL`) via `sigaction()` so that external commands terminate or stop as intended when signals are received.

---

### Q12. Why must a shell process ignore `SIGTTOU` and `SIGTTIN` during terminal ownership handover?
- According to POSIX, if a process in a background process group calls `tcsetpgrp()`, the kernel generates a `SIGTTOU` signal to prevent background processes from hijacking the terminal.
- If the shell process did not ignore `SIGTTOU`, calling `tcsetpgrp()` would cause the shell itself to be suspended by the operating system. Ignoring `SIGTTOU` permits the shell to smoothly transfer terminal control between process groups.

---

### Q13. How does `waitpid()` with `WNOHANG`, `WUNTRACED`, and `WCONTINUED` differ from basic `wait()`?
- `wait(&status)` blocks indefinitely until *any* child terminates. It cannot check child status without blocking and cannot detect when children stop or resume.
- `waitpid(pid, &status, options)` offers fine-grained control:
  - `pid = -1`: Wait for any child process.
  - `pid = -pgid`: Wait for any process in process group `pgid`.
  - `WNOHANG`: Non-blocking mode; returns `0` immediately if no child has changed state.
  - `WUNTRACED`: Reports status if a child process has been **stopped** by a signal (`SIGTSTP`, `SIGSTOP`).
  - `WCONTINUED`: Reports status if a stopped child has been **continued** by `SIGCONT`.

---

### Q14. What race condition exists between foreground waiting and `SIGCHLD`, and why is `sigsuspend()` used?
- **The Problem**: If `wait_for_job()` simply checks a loop condition and calls `pause()`, a child might terminate immediately after the check but before `pause()`. The `SIGCHLD` handler runs, and then `pause()` blocks indefinitely because the signal has already passed (missed signal race).
- **The Solution (`sigsuspend`)**:
  1. `SIGCHLD` is blocked with `sigprocmask(SIG_BLOCK, &chld_mask, &old_mask)`.
  2. While the job is not completed and not stopped, `sigsuspend(&old_mask)` is called.
  3. `sigsuspend()` **atomically** restores the unblocked mask and pauses execution until a signal occurs, eliminating the gap between checking the state and going to sleep.

---

### Q15. What makes a function "async-signal-safe", and why are `printf()` and `malloc()` forbidden in signal handlers?
- A function is **async-signal-safe** if it can be safely invoked from within a signal handler without causing deadlocks or corrupting memory, even if it interrupted another call to the same function.
- `printf()` and `malloc()` maintain internal global locks and static heap metadata. If the main thread is in the middle of modifying heap pointers during `malloc()` when interrupted by `SIGCHLD`, invoking `malloc()` or `printf()` inside the signal handler will attempt to acquire the already-held mutex, causing an immediate **deadlock** or heap corruption.
- In `mysh`, `sigchld_handler` only calls async-signal-safe system calls (`waitpid`) and updates primitive integers; all `printf()` formatting is deferred to the main REPL loop.

---

### Q16. How does `mysh` implement background execution when a line ends with `&`?
1. The lexer detects a trailing `&` token and sets `pipeline->background = 1`.
2. When forking, the shell does **not** call `tcsetpgrp()`, leaving the shell as the foreground owner of the terminal.
3. The parent registers the job in its job table with `state = JOB_RUNNING` and `is_foreground = 0`.
4. It immediately prints `[job_id] pgid` to stdout and returns control to the REPL prompt without blocking.

---

### Q17. How do `fg` and `bg` built-ins resume stopped jobs, and what signals are involved?
- **`bg %n`**:
  1. Finds job `n` in the job table.
  2. Sends `SIGCONT` to the entire process group: `kill(-job->pgid, SIGCONT)`.
  3. Updates job state to `JOB_RUNNING` with `is_foreground = 0`.
  4. Prints `[n]+ cmd &` and leaves the terminal with the shell.
- **`fg %n`**:
  1. Finds job `n` in the job table.
  2. Hands over the terminal: `tcsetpgrp(STDIN_FILENO, job->pgid)`.
  3. Sends `SIGCONT` if stopped: `kill(-job->pgid, SIGCONT)`.
  4. Updates state to `JOB_RUNNING` with `is_foreground = 1`.
  5. Waits for completion or re-suspension with `wait_for_job(job)`.
  6. Reclaims terminal: `tcsetpgrp(STDIN_FILENO, shell_pgid)`.

---

### Q18. Why does `exit` warn `"There are stopped jobs."`, and what happens on repeated `exit`?
- **Reason**: When a shell terminates, any suspended/stopped children are left in limbo without a controlling shell, frozen in memory. Standard POSIX shells protect the user from accidentally abandoning work.
- **Behavior in `mysh`**:
  - On the first `exit` attempt, if `job_count_stopped() > 0`, it prints `"There are stopped jobs."` and sets a warning flag `g_exit_warned = 1`.
  - If the user immediately types `exit` again, `mysh` sends `SIGHUP` (Hangup) followed by `SIGCONT` (Continue) to terminate all stopped process groups, cleans up all heap memory, and exits cleanly.

---

### Q19. How do environment variable expansion (`$VAR`, `$?`) and wildcard expansion work in `mysh`?
- **Variable Expansion**: When scanning unquoted text or text inside double quotes (`"..."`), the parser encounters `$`:
  - `$?`: Expands to `g_last_exit_status`, formatted as an ASCII integer.
  - `$$`: Expands to `(int)getpid()`.
  - `$NAME`: Reads alphanumeric variable name and retrieves string from `getenv("NAME")`.
  - Text inside single quotes (`'...'`) is preserved literally with no expansion.
- **Wildcard Globbing**: When an unquoted argument contains `*` or `?`, `mysh` passes the pattern to POSIX `glob()`. If matching files exist, `glob()` expands the pattern into individual arguments appended to `argv`; if no match is found, the pattern remains literal.

---

### Q20. How is memory management designed in `mysh` to guarantee zero memory leaks under Valgrind?
1. Every string duplicate allocated with `safe_strdup()` has a corresponding designated owner.
2. After each command line is executed, `free_pipeline()` recursively traverses the `Pipeline` struct, freeing every `argv[i]`, redirection filename string, `Command` array, and the pipeline object itself.
3. When background jobs terminate, `job_remove()` frees process nodes and command-line descriptions.
4. On shell exit, `jobs_cleanup()` and `history_cleanup()` free all remaining linked list allocations.
5. Result: Valgrind audits report:
   ```
   All heap blocks were freed -- no leaks are possible.
   ERROR SUMMARY: 0 errors from 0 contexts.
   ```



---

### Q21. What is a Pseudo-Terminal (pty) and why is it required for the GUI?
- A **pseudo-terminal (pty)** is a bidirectional software device pair provided by the kernel consisting of a **master** descriptor and a **slave** descriptor (`/dev/pts/N`).
- The slave behaves exactly like a real hardware terminal (TTY), supporting line discipline, canonical mode, cursor control, echoing, and signal generation (`Ctrl+C` $\to$ `SIGINT`, `Ctrl+Z` $\to$ `SIGTSTP`).
- It is required because the shell calls `tcsetpgrp()` and queries terminal attributes (`tcgetattr`, `isatty`). If the GUI simply used standard UNIX pipes (`pipe()`), `isatty()` would return false, disabling job control, process group signals, and interactive prompt handling.

---

### Q22. Why is a VTE widget used instead of a standard plain text box?
- A standard text box (`GtkTextView`) is merely an edit control for strings. It does not implement terminal escape sequences (ANSI/VT100 codes for colors, cursor positioning, clear screen).
- **VTE (Virtual Terminal Emulator)** is a native terminal emulation engine built on top of a pty. It interprets ANSI escape sequences, handles keyboard signals, passes raw keystrokes directly to the slave PTY, and provides identical terminal behavior to GNOME Terminal, xterm, or iTerm.

---

### Q23. Why is a local Named Pipe (FIFO) used instead of a network socket?
- **IPC Theme Alignment**: Named pipes (`mkfifo()`) are a fundamental UNIX IPC primitive taught in the OS syllabus, operating entirely in the kernel virtual file system (VFS) without network protocol overhead.
- **Security & Port Isolation**: Sockets bind to network interfaces or loopback ports, which can introduce security vulnerabilities, firewall prompts, and port collisions. A FIFO in `/tmp` has file permissions (`0600`), requires zero ports, and is completely immune to network eavesdropping.
- **Non-blocking Semantics**: Using `O_NONBLOCK` ensures that if the GUI crashes or closes, the shell process never hangs waiting for a reader.

---

### Q24. How does the GUI show job states (RUNNING, STOPPED, DONE) in real time?
1. Whenever the shell's job table changes state—such as when a job is created (`job_create`), stopped by `SIGTSTP` (`wait_for_job`), resumed with `SIGCONT` (`bg`/`fg`), or reaped by `SIGCHLD` (`sigchld_handler`)—the shell emits a structured one-line event message (e.g. `JOB_STATE|1|STOPPED`).
2. The message is written non-blockingly to the named pipe (`/tmp/mysh_gui_fifo_*`).
3. The GUI monitors the FIFO using `g_io_add_watch` (GTK event loop). Upon receiving an event, it parses the tokens and updates the `GtkListStore` model, coloring active running jobs green, stopped jobs amber/orange, and finished jobs grey.

---

### Q25. How do the GUI Job Control buttons (Stop, Continue, Kill) work under the hood?
- Rather than simulating actions, the GUI buttons interact directly with the operating system kernel:
  - **Stop Button**: Sends `SIGTSTP` to the process group: `kill(-job->pgid, SIGTSTP)`. The kernel halts all processes in that group.
  - **Background (bg) Button**: Sends `SIGCONT` to the process group: `kill(-job->pgid, SIGCONT)`, and sends `bg %id` into the pty.
  - **Foreground (fg) Button**: Writes `fg %id\n` directly into the PTY master, instructing `mysh` to execute `tcsetpgrp()` and bring the process group into the foreground.
  - **Kill Button**: Sends unconditional termination: `kill(-job->pgid, SIGKILL)`, terminating the entire process group instantly.
