#!/usr/bin/env python3
"""
gui_app.py - Native Desktop GUI for Mini Linux Shell (mysh)
Course: OS Lab (24B11CS312), JIIT Noida

Hard Rules Satisfied:
1. Native Desktop Application opening in its own window (Tkinter Desktop GUI).
2. NO web, NO browser, NO HTML/JS, NO localhost, NO sockets, NO ports.
3. IPC Communication uses local OS mechanisms: pseudo-terminal (pty) and named pipe (FIFO).
4. Connects directly to the real compiled C binary: ./mysh
5. Team Members: Tanishq Pundir, Anvi, Annanya Aggarwal, Palak Chauhan
"""

import os
import sys
import pty
import time
import fcntl
import struct
import termios
import signal
import errno
import re
import queue
import threading
import subprocess
import tkinter as tk
from tkinter import ttk, messagebox

PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
SHELL_BIN = os.path.join(PROJECT_DIR, "mysh")
FIFO_PATH = "/tmp/mysh_gui_fifo_{}".format(os.getpid())


class ShellGuiApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Mini Linux Shell - Process & Job Control")
        self.root.geometry("1100x720")
        self.root.configure(bg="#0f172a")

        self.jobs = {}       # job_id -> {pgid, cmd, state}
        self.stages = []     # list of stage names
        self.cwd = os.getcwd()
        self.last_exit = 0
        self.master_fd = -1
        self.shell_pid = -1
        self.shell_alive = False

        # Thread-safe communication queues from worker threads to main GUI thread
        self.pty_queue = queue.Queue()
        self.fifo_queue = queue.Queue()

        # Setup FIFO for shell event IPC
        if os.path.exists(FIFO_PATH):
            os.remove(FIFO_PATH)
        os.mkfifo(FIFO_PATH, 0o600)
        self.fifo_fd = os.open(FIFO_PATH, os.O_RDWR | os.O_NONBLOCK)

        # Style configuration
        self.style = ttk.Style()
        self.style.theme_use("clam")
        self.style.configure(".", background="#0f172a", foreground="#e2e8f0",
                             fieldbackground="#1e293b")
        self.style.configure("Treeview", background="#1e293b", foreground="#e2e8f0",
                             fieldbackground="#1e293b", rowheight=24)
        self.style.map("Treeview",
                       background=[("selected", "#0284c7")],
                       foreground=[("selected", "#ffffff")])

        # Toolbar button styles (macOS-friendly ttk buttons)
        self.style.configure("Green.TButton", background="#10b981", foreground="#ffffff",
                             font=("Helvetica", 10, "bold"), padding=4)
        self.style.map("Green.TButton",
                       background=[("active", "#0f9e6e")],
                       foreground=[("active", "#ffffff")])
        self.style.configure("Blue.TButton", background="#0284c7", foreground="#ffffff",
                             font=("Helvetica", 9), padding=4)
        self.style.map("Blue.TButton",
                       background=[("active", "#0369a1")],
                       foreground=[("active", "#ffffff")])
        self.style.configure("Gray.TButton", background="#334155", foreground="#f1f5f9",
                             font=("Helvetica", 9), padding=4)
        self.style.map("Gray.TButton",
                       background=[("active", "#475569")],
                       foreground=[("active", "#ffffff")])
        # Job control button styles
        self.style.configure("Yellow.TButton", background="#f59e0b", foreground="#000000",
                             font=("Helvetica", 8, "bold"), padding=2)
        self.style.map("Yellow.TButton",
                       background=[("active", "#d97706")])
        self.style.configure("Red.TButton", background="#ef4444", foreground="#ffffff",
                             font=("Helvetica", 8, "bold"), padding=2)
        self.style.map("Red.TButton",
                       background=[("active", "#dc2626")])

        self.create_widgets()
        self.start_shell_process()
        self.start_fifo_reader()
        self.root.after(10, self._poll_queues)

    # ------------------------------------------------------------------ #
    #  Widget creation                                                     #
    # ------------------------------------------------------------------ #
    def create_widgets(self):
        # 1. Header Toolbar
        self.toolbar = tk.Frame(self.root, bg="#1e293b", pady=6, padx=8)
        self.toolbar.pack(fill=tk.X, side=tk.TOP)

        ttk.Button(self.toolbar, text="Run Demo", style="Green.TButton",
                   command=self.run_demo_mode).pack(side=tk.LEFT, padx=4)
        ttk.Button(self.toolbar, text="Pipeline Demo", style="Gray.TButton",
                   command=lambda: self.send_to_pty("cat src/*.c | grep include | wc -l\n")).pack(side=tk.LEFT, padx=3)
        ttk.Button(self.toolbar, text="sleep 15 &", style="Gray.TButton",
                   command=lambda: self.send_to_pty("sleep 15 &\n")).pack(side=tk.LEFT, padx=3)
        ttk.Button(self.toolbar, text="pwd", style="Gray.TButton",
                   command=lambda: self.send_to_pty("pwd\n")).pack(side=tk.LEFT, padx=3)
        ttk.Button(self.toolbar, text="Clear Terminal", style="Gray.TButton",
                   command=self.clear_terminal).pack(side=tk.LEFT, padx=3)
        ttk.Button(self.toolbar, text="New Session", style="Green.TButton",
                   command=self.start_new_session).pack(side=tk.LEFT, padx=3)
        ttk.Button(self.toolbar, text="Team & About", style="Blue.TButton",
                   command=self.show_about_dialog).pack(side=tk.RIGHT, padx=4)

        # 2. Main Paned Layout
        paned = tk.PanedWindow(self.root, orient=tk.HORIZONTAL, bg="#0f172a", sashwidth=4)
        paned.pack(fill=tk.BOTH, expand=True, padx=6, pady=4)

        # 2a. Left Pane: Terminal Emulator
        left_frame = tk.Frame(paned, bg="#000000")
        paned.add(left_frame, minsize=520)

        tk.Label(left_frame, text="mysh Embedded Terminal (/dev/pty)", bg="#090d16",
                 fg="#94a3b8", font=("Courier", 10, "bold"), anchor="w",
                 padx=8, pady=4).pack(fill=tk.X)

        self.term_text = tk.Text(left_frame, bg="#05070d", fg="#38bdf8",
                                 insertbackground="#38bdf8", font=("Menlo", 11),
                                 wrap="char", relief="flat", padx=8, pady=8)
        self.term_text.pack(fill=tk.BOTH, expand=True)
        self.term_text.bind("<Key>", self.on_term_key)
        self.term_text.bind("<BackSpace>", self.on_backspace)
        self.term_text.bind("<Delete>", self.on_backspace)
        self.term_text.bind("<KP_Delete>", self.on_backspace)

        # 2b. Right Pane: Visual Monitor Panels
        right_frame = tk.Frame(paned, bg="#0f172a")
        paned.add(right_frame, minsize=480)

        # --- Jobs Panel ---
        tk.Label(right_frame, text="Active Jobs (Job Control)", bg="#0f172a",
                 fg="#f8fafc", font=("Helvetica", 10, "bold"), anchor="w"
                 ).pack(fill=tk.X, pady=(2, 2))

        cols = ("ID", "PGID", "Command", "State")
        self.jobs_tree = ttk.Treeview(right_frame, columns=cols, show="headings", height=4)
        for c in cols:
            self.jobs_tree.heading(c, text=c)
        self.jobs_tree.column("ID", width=40, anchor="center")
        self.jobs_tree.column("PGID", width=70, anchor="center")
        self.jobs_tree.column("Command", width=220)
        self.jobs_tree.column("State", width=80, anchor="center")
        self.jobs_tree.pack(fill=tk.X)

        # Job Control Buttons
        btn_frame = tk.Frame(right_frame, bg="#0f172a", pady=4)
        btn_frame.pack(fill=tk.X)
        ttk.Button(btn_frame, text="Stop (SIGTSTP)", style="Yellow.TButton",
                   command=self.job_stop).pack(side=tk.LEFT, padx=2, expand=True, fill=tk.X)
        ttk.Button(btn_frame, text="bg (SIGCONT)", style="Green.TButton",
                   command=self.job_bg).pack(side=tk.LEFT, padx=2, expand=True, fill=tk.X)
        ttk.Button(btn_frame, text="fg", style="Blue.TButton",
                   command=self.job_fg).pack(side=tk.LEFT, padx=2, expand=True, fill=tk.X)
        ttk.Button(btn_frame, text="Kill (SIGKILL)", style="Red.TButton",
                   command=self.job_kill).pack(side=tk.LEFT, padx=2, expand=True, fill=tk.X)

        # --- Pipeline Visualizer ---
        tk.Label(right_frame, text="IPC Pipeline Visualizer", bg="#0f172a",
                 fg="#f8fafc", font=("Helvetica", 10, "bold"), anchor="w"
                 ).pack(fill=tk.X, pady=(6, 2))
        self.pipe_canvas = tk.Canvas(right_frame, bg="#090d16", height=65,
                                     highlightthickness=1, highlightbackground="#334155")
        self.pipe_canvas.pack(fill=tk.X)
        self.draw_pipeline_stages()

        # --- Process Info Panel ---
        tk.Label(right_frame, text="Process Hierarchy (ps)", bg="#0f172a",
                 fg="#f8fafc", font=("Helvetica", 10, "bold"), anchor="w"
                 ).pack(fill=tk.X, pady=(6, 2))

        pcols = ("PID", "PPID", "PGID", "State", "Command")
        self.proc_tree = ttk.Treeview(right_frame, columns=pcols, show="headings", height=4)
        for c in pcols:
            self.proc_tree.heading(c, text=c)
        self.proc_tree.column("PID", width=55, anchor="center")
        self.proc_tree.column("PPID", width=55, anchor="center")
        self.proc_tree.column("PGID", width=55, anchor="center")
        self.proc_tree.column("State", width=45, anchor="center")
        self.proc_tree.column("Command", width=180)
        self.proc_tree.pack(fill=tk.X)

        # --- Signal Log ---
        tk.Label(right_frame, text="Signal & Kernel Event Log", bg="#0f172a",
                 fg="#f8fafc", font=("Helvetica", 10, "bold"), anchor="w"
                 ).pack(fill=tk.X, pady=(6, 2))
        self.sig_text = tk.Text(right_frame, bg="#090d16", fg="#94a3b8", height=6,
                                font=("Menlo", 9), relief="flat", padx=6, pady=4)
        self.sig_text.pack(fill=tk.BOTH, expand=True)

        # 3. Status Bar
        self.status_bar = tk.Label(self.root, text="Ready", bg="#1e293b", fg="#cbd5e1",
                                   font=("Helvetica", 9), anchor="w", padx=10, pady=3)
        self.status_bar.pack(fill=tk.X, side=tk.BOTTOM)
        self.update_status()

    # ------------------------------------------------------------------ #
    #  Shell process management (pty.fork)                                 #
    # ------------------------------------------------------------------ #
    def start_shell_process(self):
        """Start ./mysh inside a real pseudo-terminal using pty.fork()."""
        pid, fd = pty.fork()
        if pid == 0:
            # --- CHILD ---
            try:
                attrs = termios.tcgetattr(0)
                attrs[3] |= (termios.ICANON | termios.ECHO | termios.ECHOE | termios.ECHOK)
                attrs[6][termios.VERASE] = b'\x7f'
                termios.tcsetattr(0, termios.TCSANOW, attrs)
            except Exception:
                pass
            os.chdir(PROJECT_DIR)
            os.execv(SHELL_BIN, [SHELL_BIN, "--gui-fifo", FIFO_PATH])
            os._exit(127)
        else:
            # --- PARENT ---
            self.master_fd = fd
            self.shell_pid = pid
            self.shell_alive = True

            try:
                attrs = termios.tcgetattr(fd)
                attrs[3] |= (termios.ICANON | termios.ECHO | termios.ECHOE | termios.ECHOK)
                attrs[6][termios.VERASE] = b'\x7f'
                termios.tcsetattr(fd, termios.TCSANOW, attrs)
            except Exception:
                pass

            # Set the master fd to non-blocking so reads don't hang
            flags = fcntl.fcntl(fd, fcntl.F_GETFL)
            fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

            # Set initial terminal size
            try:
                rows, cols = 24, 80
                winsize = struct.pack("HHHH", rows, cols, 0, 0)
                fcntl.ioctl(fd, termios.TIOCSWINSZ, winsize)
            except Exception:
                pass

            # Start reader thread
            t = threading.Thread(target=self.pty_reader_thread, daemon=True)
            t.start()

            # Start periodic process poll
            self.root.after(1000, self.poll_processes)

    def _poll_queues(self):
        """Processes messages from background threads exclusively on the main Tk thread."""
        try:
            while not self.pty_queue.empty():
                kind, payload = self.pty_queue.get_nowait()
                if kind == "TEXT":
                    self.append_term_text(payload)
                elif kind == "DIED":
                    self._on_shell_died(payload)
        except Exception:
            pass

        try:
            while not self.fifo_queue.empty():
                line = self.fifo_queue.get_nowait()
                self.handle_fifo_event(line)
        except Exception:
            pass

        self.root.after(10, self._poll_queues)

    def pty_reader_thread(self):
        """Read output from the shell's pty and dispatch via thread-safe queue."""
        while self.shell_alive:
            try:
                data = os.read(self.master_fd, 4096)
                if not data:
                    self.pty_queue.put(("DIED", "Shell closed (EOF)"))
                    break
                text = data.decode(errors="replace")
                self.pty_queue.put(("TEXT", text))
            except OSError as e:
                if e.errno == errno.EIO:
                    self.pty_queue.put(("DIED", "Shell exited (EIO)"))
                    break
                elif e.errno == errno.EAGAIN:
                    time.sleep(0.01)
                else:
                    self.pty_queue.put(("DIED", f"Shell error: {e}"))
                    break

    def _on_shell_died(self, reason):
        """Handle shell death from the main thread."""
        if not self.shell_alive:
            return
        self.shell_alive = False
        # Reap child
        exit_msg = reason
        try:
            _, status = os.waitpid(self.shell_pid, os.WNOHANG)
            if os.WIFEXITED(status):
                exit_msg = "mysh exited with code {}".format(os.WEXITSTATUS(status))
            elif os.WIFSIGNALED(status):
                exit_msg = "mysh killed by signal {}".format(os.WTERMSIG(status))
        except Exception:
            pass
        self.append_term_text("\n\n--- {} ---\n".format(exit_msg))
        self.log_event(exit_msg)
        try:
            os.close(self.master_fd)
        except Exception:
            pass
        self.master_fd = -1

    def _safe_write_pty(self, data):
        """Write bytes to the pty master, returning True on success."""
        if not self.shell_alive or self.master_fd < 0:
            self.log_event("Shell not running. Click 'New Session' to restart.")
            return False
        try:
            os.write(self.master_fd, data)
            return True
        except OSError:
            self._on_shell_died("Write failed (shell died)")
            return False

    # ------------------------------------------------------------------ #
    #  Terminal widget I/O                                                 #
    # ------------------------------------------------------------------ #
    def append_term_text(self, text):
        """Append text received from pty to terminal widget, handling backspace/erase."""
        if not text:
            return

        # Strip ANSI escape sequences (e.g. colors)
        text = re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)

        i = 0
        n = len(text)
        buf = []

        def flush():
            if buf:
                self.term_text.insert(tk.END, ''.join(buf))
                buf.clear()

        while i < n:
            # Standard terminal erase sequence echoed by pty: \x08 \x08 (BS, Space, BS)
            if text[i:i+3] == "\x08 \x08":
                flush()
                content = self.term_text.get("1.0", "end-1c")
                if content:
                    self.term_text.delete("end-2c")
                i += 3
            # Single backspace character or DEL
            elif text[i] == "\x08" or text[i] == "\x7f":
                flush()
                content = self.term_text.get("1.0", "end-1c")
                if content:
                    self.term_text.delete("end-2c")
                i += 1
            # Normalise CRLF to LF
            elif text[i] == "\r" and (i + 1 < n and text[i+1] == "\n"):
                buf.append("\n")
                i += 2
            elif text[i] == "\r":
                i += 1
            else:
                buf.append(text[i])
                i += 1

        flush()
        self.term_text.see(tk.END)

    def on_backspace(self, event=None):
        """Explicit handler for BackSpace and Delete keys."""
        self._safe_write_pty(b"\x7f")
        return "break"

    def on_term_key(self, event):
        """Forward keystrokes from the Tk Text widget to the pty as raw bytes."""
        ctrl = bool(event.state & 0x4)   # Control key mask on macOS Tk

        # Control-key combos first
        if ctrl:
            cmap = {
                "c": b"\x03",   # SIGINT
                "z": b"\x1a",   # SIGTSTP
                "d": b"\x04",   # EOF
                "l": b"\x0c",   # clear
                "h": b"\x7f",   # Ctrl+H is backspace
                "w": b"\x17",   # Ctrl+W is erase word
                "u": b"\x15",   # Ctrl+U is erase line
            }
            key = event.keysym.lower()
            if key in cmap:
                self._safe_write_pty(cmap[key])
            return "break"

        # Backspace / Delete keys
        if event.keysym in ("BackSpace", "Delete", "KP_Delete") or event.char in ("\x08", "\x7f"):
            return self.on_backspace(event)

        # Named special keys
        keymap = {
            "Return":    b"\n",
            "Tab":       b"\t",
            "Up":        b"\x1b[A",
            "Down":      b"\x1b[B",
            "Left":      b"\x1b[D",
            "Right":     b"\x1b[C",
        }
        if event.keysym in keymap:
            self._safe_write_pty(keymap[event.keysym])
            return "break"

        # Printable characters
        if event.char and event.char.isprintable():
            self._safe_write_pty(event.char.encode())
            return "break"

        return "break"

    def send_to_pty(self, text):
        """Send a string to the shell (used by toolbar buttons)."""
        self._safe_write_pty(text.encode())

    def clear_terminal(self):
        self.term_text.delete("1.0", tk.END)

    def start_new_session(self):
        """Kill the old shell and start a fresh one."""
        # Kill old shell
        if self.shell_alive:
            try:
                os.killpg(os.getpgid(self.shell_pid), signal.SIGHUP)
            except Exception:
                pass
            try:
                os.waitpid(self.shell_pid, 0)
            except Exception:
                pass
        self.shell_alive = False
        try:
            if self.master_fd >= 0:
                os.close(self.master_fd)
        except Exception:
            pass
        self.master_fd = -1
        self.jobs.clear()
        self.refresh_jobs_tree()
        self.clear_terminal()
        self.start_shell_process()
        self.log_event("Started new shell session")

    # ------------------------------------------------------------------ #
    #  FIFO reader (shell -> GUI events)                                   #
    # ------------------------------------------------------------------ #
    def start_fifo_reader(self):
        t = threading.Thread(target=self.fifo_reader_thread, daemon=True)
        t.start()

    def fifo_reader_thread(self):
        buf = ""
        while True:
            try:
                data = os.read(self.fifo_fd, 4096)
                if data:
                    buf += data.decode(errors="replace")
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip()
                        if line:
                            self.fifo_queue.put(line)
                else:
                    time.sleep(0.05)
            except OSError:
                time.sleep(0.05)

    def handle_fifo_event(self, line):
        parts = line.split("|")
        etype = parts[0]

        if etype == "JOB_ADD" and len(parts) >= 5:
            jid = int(parts[1])
            pgid = int(parts[2])
            cmd = parts[3]
            state = parts[4]
            self.jobs[jid] = {"pgid": pgid, "cmd": cmd, "state": state}
            self.refresh_jobs_tree()
            self.log_event("JOB_ADD: [{}] PGID {} - {}".format(jid, pgid, cmd))

        elif etype == "JOB_STATE" and len(parts) >= 3:
            jid = int(parts[1])
            state = parts[2]
            if jid in self.jobs:
                self.jobs[jid]["state"] = state
                self.refresh_jobs_tree()
                self.log_event("JOB_STATE: [{}] state -> {}".format(jid, state))

        elif etype == "JOB_DONE" and len(parts) >= 3:
            jid = int(parts[1])
            code = parts[2]
            if jid in self.jobs:
                del self.jobs[jid]
                self.refresh_jobs_tree()
                self.log_event("JOB_DONE: [{}] finished (code {})".format(jid, code))

        elif etype == "PIPE_CREATE" and len(parts) >= 2:
            n = int(parts[1])
            self.stages = ["Stage {}".format(i + 1) for i in range(n)]
            self.draw_pipeline_stages()
            self.log_event("PIPE_CREATE: Created {}-stage IPC pipeline".format(n))

        elif etype == "PROC_NEW" and len(parts) >= 4:
            pid_s = parts[1]
            pgid_s = parts[2]
            cmd = parts[3]
            self.log_event("PROC_NEW: Forked child PID {} (PGID {}): {}".format(pid_s, pgid_s, cmd))

        elif etype == "SIG_EVENT" and len(parts) >= 3:
            sig = parts[1]
            desc = parts[2]
            self.log_event("{} -> {}".format(sig, desc))

        elif etype == "CWD_CHANGE" and len(parts) >= 2:
            self.cwd = parts[1]
            self.update_status()

    # ------------------------------------------------------------------ #
    #  UI helpers                                                          #
    # ------------------------------------------------------------------ #
    def refresh_jobs_tree(self):
        for item in self.jobs_tree.get_children():
            self.jobs_tree.delete(item)
        for jid, info in sorted(self.jobs.items()):
            self.jobs_tree.insert("", tk.END, values=(jid, info["pgid"], info["cmd"], info["state"]))
        self.update_status()

    def draw_pipeline_stages(self):
        self.pipe_canvas.delete("all")
        if not self.stages:
            self.pipe_canvas.create_text(20, 32,
                text="No active pipeline (run e.g. ls | grep txt | wc -l)",
                fill="#64748b", anchor="w", font=("Helvetica", 9))
            return

        w, h = 85, 36
        spacing = 40
        start_x = 20
        y = 15
        for i, stage in enumerate(self.stages):
            x = start_x + i * (w + spacing)
            self.pipe_canvas.create_rectangle(x, y, x + w, y + h,
                                              fill="#1e293b", outline="#10b981", width=2)
            self.pipe_canvas.create_text(x + w / 2, y + h / 2, text=stage,
                                         fill="#f8fafc", font=("Menlo", 9, "bold"))
            if i < len(self.stages) - 1:
                ax1 = x + w + 4
                ax2 = ax1 + spacing - 8
                ay = y + h / 2
                self.pipe_canvas.create_line(ax1, ay, ax2, ay,
                                             fill="#64748b", width=2, arrow=tk.LAST)
                self.pipe_canvas.create_text(ax1 + spacing / 2 - 2, ay - 8,
                                             text="pipe", fill="#eab308", font=("Helvetica", 7))

    def poll_processes(self):
        """Query child processes using ps (works on macOS, no /proc needed)."""
        for item in self.proc_tree.get_children():
            self.proc_tree.delete(item)
        if self.shell_alive and self.shell_pid > 0:
            try:
                res = subprocess.check_output(
                    ["ps", "-o", "pid,ppid,pgid,state,command", "-g", str(self.shell_pid)],
                    text=True, stderr=subprocess.DEVNULL)
                for line in res.strip().split("\n")[1:]:
                    parts = line.split(None, 4)
                    if len(parts) >= 5:
                        self.proc_tree.insert("", tk.END, values=parts)
            except Exception:
                pass
        if self.shell_alive:
            self.root.after(1000, self.poll_processes)

    def log_event(self, text):
        tstr = time.strftime("%H:%M:%S")
        self.sig_text.insert(tk.END, "[{}] {}\n".format(tstr, text))
        self.sig_text.see(tk.END)

    def update_status(self):
        running = sum(1 for j in self.jobs.values() if j["state"] == "RUNNING")
        stopped = sum(1 for j in self.jobs.values() if j["state"] == "STOPPED")
        self.status_bar.config(
            text=" Directory: {}   |   Running Jobs: {}   |   Stopped Jobs: {}   |   Last Exit: {}".format(
                self.cwd, running, stopped, self.last_exit))

    # ------------------------------------------------------------------ #
    #  Job control buttons                                                 #
    # ------------------------------------------------------------------ #
    def get_selected_job(self):
        sel = self.jobs_tree.selection()
        if not sel:
            messagebox.showinfo("Job Control", "Please select a job in the table first.")
            return None
        vals = self.jobs_tree.item(sel[0])["values"]
        return int(vals[0]), int(vals[1])

    def job_stop(self):
        res = self.get_selected_job()
        if res:
            jid, pgid = res
            try:
                os.kill(-pgid, signal.SIGTSTP)
                self.log_event("Sent SIGTSTP to Process Group -{}".format(pgid))
            except OSError as e:
                self.log_event("SIGTSTP failed: {}".format(e))

    def job_bg(self):
        res = self.get_selected_job()
        if res:
            jid, pgid = res
            self.send_to_pty("bg %{}\n".format(jid))
            self.log_event("Sent bg command to Job [{}] (PGID {})".format(jid, pgid))

    def job_fg(self):
        res = self.get_selected_job()
        if res:
            jid, pgid = res
            self.send_to_pty("fg %{}\n".format(jid))
            self.log_event("Sent fg command to Job [{}] (PGID {})".format(jid, pgid))

    def job_kill(self):
        res = self.get_selected_job()
        if res:
            jid, pgid = res
            try:
                os.kill(-pgid, signal.SIGKILL)
                self.log_event("Sent SIGKILL to Process Group -{}".format(pgid))
            except OSError as e:
                self.log_event("SIGKILL failed: {}".format(e))

    # ------------------------------------------------------------------ #
    #  Demo mode                                                           #
    # ------------------------------------------------------------------ #
    def run_demo_mode(self):
        """Execute an automated demo sequence with staggered intervals."""
        demo_cmds = [
            ("echo '=== DEMO: I/O Redirection ==='", 1),
            ("echo 'testing file redirection' > demo.txt ; cat < demo.txt", 2),
            ("echo '=== DEMO: 3-Stage Pipeline ==='", 3),
            ("cat src/*.c | grep 'include' | wc -l", 4),
            ("echo '=== DEMO: Background Job Execution ==='", 5),
            ("sleep 15 &", 6),
            ("jobs", 7),
            ("echo '=== Demo Sequence Finished ==='", 8),
        ]
        for cmd, sec in demo_cmds:
            self.root.after(sec * 1200, lambda c=cmd: self.send_to_pty("{}\n".format(c)))

    # ------------------------------------------------------------------ #
    #  About dialog                                                        #
    # ------------------------------------------------------------------ #
    def show_about_dialog(self):
        msg = ("Mini Linux Shell (mysh) - Native Desktop GUI\n\n"
               "Operating Systems Lab (24B11CS312)\n"
               "Jaypee Institute of Information Technology (JIIT), Noida\n\n"
               "Team Members:\n"
               "  Tanishq Pundir\n"
               "  Anvi\n"
               "  Annanya Aggarwal\n"
               "  Palak Chauhan\n\n"
               "Architecture:\n"
               "Native Desktop Window <-> pseudo-terminal (pty) <-> ./mysh (C binary)\n"
               "Status updates via non-blocking named pipe (FIFO) in /tmp.\n"
               "Zero web sockets, zero localhost, zero network dependencies.")
        messagebox.showinfo("About - Mini Linux Shell", msg)

    # ------------------------------------------------------------------ #
    #  Window close                                                        #
    # ------------------------------------------------------------------ #
    def on_close(self):
        """Clean termination of shell and FIFO."""
        if self.shell_alive:
            try:
                os.killpg(os.getpgid(self.shell_pid), signal.SIGHUP)
                os.killpg(os.getpgid(self.shell_pid), signal.SIGCONT)
            except Exception:
                pass
        self.shell_alive = False
        try:
            if self.master_fd >= 0:
                os.close(self.master_fd)
        except Exception:
            pass
        try:
            os.close(self.fifo_fd)
        except Exception:
            pass
        if os.path.exists(FIFO_PATH):
            try:
                os.remove(FIFO_PATH)
            except Exception:
                pass
        self.root.destroy()


def main():
    root = tk.Tk()
    app = ShellGuiApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()


if __name__ == "__main__":
    main()
