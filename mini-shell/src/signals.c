/**
 * signals.c - Implementation of signal handling and job control
 * 
 * OS Concept: POSIX Signal Handling, Asynchronous Events & Process Groups
 * Demonstrates:
 * 1. Protecting the shell from keyboard interrupt signals (SIGINT, SIGTSTP).
 * 2. Restoring default signal handlers in child processes.
 * 3. Asynchronously reaping terminated or stopped children with SIGCHLD.
 * 4. Preventing race conditions between SIGCHLD handler and main thread
 *    via atomic masks (sigprocmask).
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#include "signals.h"
#include "jobs.h"
#include "gui_events.h"
#include <sys/wait.h>
#include <unistd.h>

pid_t shell_pgid = 0;
int shell_is_interactive = 0;
volatile sig_atomic_t g_child_event = 0;

void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    int status;
    pid_t pid;

    /*
     * Reap all available child status changes without blocking.
     * WNOHANG: return immediately if no child has exited.
     * WUNTRACED: report status of stopped children (e.g. Ctrl+Z).
     * WCONTINUED: report status of continued children (e.g. SIGCONT).
     */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        job_update_process_status(pid, status);
        gui_event_emit("SIG_EVENT|SIGCHLD|Reaped child status pid=%d", (int)pid);
        g_child_event = 1;
    }

    errno = saved_errno;
}

static void sigint_prompt_handler(int sig) {
    (void)sig;
    /* Interrupted input reading at prompt; shell continues */
}

void shell_init_job_control(void) {
    /* Check if standard input is associated with an interactive terminal */
    shell_is_interactive = isatty(STDIN_FILENO);
    if (!shell_is_interactive) {
        shell_pgid = getpgrp();
        return;
    }

    /* Previously we tried to ensure the shell is in the foreground process group
       using a loop with tcgetpgrp() and SIGTTIN. This can dead‑lock when the
       process is started via a PTY without an actual controlling terminal.
       Instead we simply place the shell in its own process group and
       attempt to take terminal control once; any failure is non‑fatal. */
    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0) {
        /* EPERM is normal if we are already a session leader */
        if (errno != EPERM) {
            perror("mysh: setpgid");
        }
    }
    /* Attempt to make the shell the foreground process group – ignore errors */
    if (tcsetpgrp(STDIN_FILENO, shell_pgid) < 0) {
        /* Non‑fatal – keep the current process group */
        shell_pgid = getpgrp();
    }
}

void signals_init(void) {
    struct sigaction sa_ign;
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags = 0;

    /* Ignore terminal signals in the shell */
    sigaction(SIGTSTP, &sa_ign, NULL);
    sigaction(SIGQUIT, &sa_ign, NULL);
    sigaction(SIGTTIN, &sa_ign, NULL);
    sigaction(SIGTTOU, &sa_ign, NULL);

    /*
     * SIGINT handler without SA_RESTART:
     * When user presses Ctrl+C at the empty prompt, getline() returns EINTR,
     * allowing mysh to print a newline and a fresh prompt without terminating.
     */
    struct sigaction sa_int;
    sa_int.sa_handler = sigint_prompt_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    /* Install SIGCHLD handler to reap background children asynchronously */
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    /* SA_RESTART: automatically restart interrupted library calls
       SA_NOCLDSTOP is omitted so stopped children trigger SIGCHLD */
    sa_chld.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);
}

void signals_restore_defaults(void) {
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    /* Restore default signal dispositions for children */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL);
    sigaction(SIGTTOU, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);

    /* Unblock all signals */
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);
}

void signals_block_chld(sigset_t *old_mask) {
    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_mask, old_mask);
}

void signals_unblock_chld(const sigset_t *old_mask) {
    sigprocmask(SIG_SETMASK, old_mask, NULL);
}

void terminal_give_to(pid_t pgid) {
    if (shell_is_interactive && pgid > 0) {
        tcsetpgrp(STDIN_FILENO, pgid);
    }
}

void terminal_reclaim(void) {
    if (shell_is_interactive && shell_pgid > 0) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
    }
}
