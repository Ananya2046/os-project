/**
 * signals.h - Signal handling and terminal job control interface
 * 
 * OS Concept: POSIX Signals, Signal Masking, and Terminal Process Groups
 * Configures signal actions (SIGINT, SIGTSTP, SIGQUIT, SIGTTIN, SIGTTOU, SIGCHLD)
 * and manages terminal ownership via tcsetpgrp().
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#ifndef SIGNALS_H
#define SIGNALS_H

#include "utils.h"
#include <signal.h>
#include <sys/types.h>

/* Global state for shell process and terminal */
extern pid_t shell_pgid;
extern int shell_is_interactive;
extern volatile sig_atomic_t g_child_event;

/**
 * Initializes shell job control: puts shell in its own process group
 * and acquires terminal foreground ownership.
 */
void shell_init_job_control(void);

/**
 * Configures signal actions for the interactive shell process.
 * Ignores SIGINT, SIGTSTP, SIGQUIT, SIGTTIN, SIGTTOU; installs SIGCHLD handler.
 */
void signals_init(void);

/**
 * Restores default signal dispositions (SIG_DFL) in child processes before execvp().
 */
void signals_restore_defaults(void);

/**
 * Blocks SIGCHLD to protect critical sections accessing the job table.
 */
void signals_block_chld(sigset_t *old_mask);

/**
 * Restores the previous signal mask after exiting a critical section.
 */
void signals_unblock_chld(const sigset_t *old_mask);

/**
 * Transfers terminal control to the specified process group.
 */
void terminal_give_to(pid_t pgid);

/**
 * Reclaims terminal control back to the shell process group.
 */
void terminal_reclaim(void);

#endif /* SIGNALS_H */

