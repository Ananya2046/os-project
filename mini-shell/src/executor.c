/**
 * executor.c - Implementation of command execution and pipeline orchestration
 * 
 * OS Concepts Demonstrated:
 * 1. Process Creation (fork): Creating isolated child execution contexts.
 * 2. Program Overlay (execvp): Replacing child process image with binary.
 * 3. Inter-Process Communication (pipe): Connecting stdout of stage N to stdin of stage N+1.
 * 4. File Descriptor Manipulation (dup2, open, close): I/O redirection.
 * 5. Process Groups & Terminal Ownership (setpgid, tcsetpgrp): Avoiding race conditions.
 * 6. Process Synchronization (waitpid, sigsuspend): Reaping and status monitoring.
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#include "executor.h"
#include "builtins.h"
#include "signals.h"
#include "gui_events.h"
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

int wait_for_job(Job *job) {
    if (!job) return 0;

    /* Transfer foreground terminal control to the job's process group */
    terminal_give_to(job->pgid);

    sigset_t old_mask;
    signals_block_chld(&old_mask);

    /* Reap any events already pending before entering suspend loop */
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        job_update_process_status(pid, status);
    }

    /*
     * Textbook POSIX synchronization:
     * While job is still running in foreground and not stopped,
     * atomically unblock SIGCHLD and pause using sigsuspend().
     */
    while (!job_is_completed(job) && !job_is_stopped(job)) {
        sigsuspend(&old_mask);
        while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
            job_update_process_status(pid, status);
        }
    }

    signals_unblock_chld(&old_mask);

    /* Reclaim terminal control back to the shell process group */
    terminal_reclaim();

    /* Identify status of the last process in the pipeline */
    int last_status = 0;
    Process *p = job->first_process;
    while (p) {
        last_status = p->status;
        p = p->next;
    }

    if (job_is_stopped(job)) {
        job->state = JOB_STOPPED;
        job->is_foreground = 0;
        job->notified = 1;
        gui_event_emit("SIG_EVENT|SIGTSTP|Job [%d] stopped", job->job_id);
        gui_event_emit("JOB_STATE|%d|STOPPED", job->job_id);
        printf("\n[%d]+  Stopped                 %s\n", job->job_id, job->command_line);
        fflush(stdout);
    } else {
        gui_event_emit("JOB_DONE|%d|%d", job->job_id, last_status);
        /* Print signal termination messages (e.g. Terminated, Segmentation fault) */
        if (WIFSIGNALED(last_status)) {
            int sig = WTERMSIG(last_status);
            if (sig == SIGINT) {
                printf("\n");
            } else {
                printf("%s\n", strsignal(sig));
            }
            fflush(stdout);
        }
        job_remove(job);
    }

    if (WIFEXITED(last_status)) {
        return WEXITSTATUS(last_status);
    } else if (WIFSIGNALED(last_status)) {
        return 128 + WTERMSIG(last_status);
    }
    return 0;
}

static int execute_builtin_with_redirection(Command *cmd) {
    int saved_stdin = -1;
    int saved_stdout = -1;
    int saved_stderr = -1;
    int ret = 0;

    if (cmd->input_file) {
        saved_stdin = dup(STDIN_FILENO);
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            shell_perror(cmd->input_file);
            close(saved_stdin);
            return 1;
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (cmd->output_file) {
        saved_stdout = dup(STDOUT_FILENO);
        int flags = O_WRONLY | O_CREAT | (cmd->append_output ? O_APPEND : O_TRUNC);
        int fd = open(cmd->output_file, flags, 0644);
        if (fd < 0) {
            shell_perror(cmd->output_file);
            if (saved_stdin != -1) { dup2(saved_stdin, STDIN_FILENO); close(saved_stdin); }
            close(saved_stdout);
            return 1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    if (cmd->error_file) {
        saved_stderr = dup(STDERR_FILENO);
        int fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            shell_perror(cmd->error_file);
            if (saved_stdin != -1) { dup2(saved_stdin, STDIN_FILENO); close(saved_stdin); }
            if (saved_stdout != -1) { dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout); }
            close(saved_stderr);
            return 1;
        }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    ret = execute_builtin(cmd);

    /* Restore original standard descriptors */
    if (saved_stdin != -1) {
        dup2(saved_stdin, STDIN_FILENO);
        close(saved_stdin);
    }
    if (saved_stdout != -1) {
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }
    if (saved_stderr != -1) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
    }

    return ret;
}

int execute_pipeline(Pipeline *pipeline) {
    if (!pipeline || pipeline->num_commands == 0) return 0;

    int ret = 0;

    /* Single built-in command without backgrounding runs directly in parent */
    if (pipeline->num_commands == 1 &&
        is_builtin(pipeline->commands[0].argv[0]) &&
        !pipeline->background) {
        ret = execute_builtin_with_redirection(&pipeline->commands[0]);
        g_last_exit_status = ret;
        return ret;
    }

    int num_cmds = pipeline->num_commands;
    int (*pipefds)[2] = NULL;

    /* Allocate and create pipes for multi-stage pipelines */
    if (num_cmds > 1) {
        gui_event_emit("PIPE_CREATE|%d", num_cmds);
        pipefds = safe_malloc(sizeof(int[2]) * (num_cmds - 1));
        for (int i = 0; i < num_cmds - 1; i++) {
            if (pipe(pipefds[i]) < 0) {
                shell_perror("pipe");
                for (int j = 0; j < i; j++) {
                    close(pipefds[j][0]);
                    close(pipefds[j][1]);
                }
                free(pipefds);
                return -1;
            }
        }
    }

    /* Register job in job table */
    Job *job = job_create(0, pipeline->raw_input, !pipeline->background);

    for (int i = 0; i < num_cmds; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            shell_perror("fork");
            if (pipefds) {
                for (int j = 0; j < num_cmds - 1; j++) {
                    close(pipefds[j][0]);
                    close(pipefds[j][1]);
                }
                free(pipefds);
            }
            return -1;
        }

        if (pid == 0) {
            /* ==================================================== */
            /* CHILD PROCESS EXECUTION                              */
            /* ==================================================== */

            /*
             * 1. Set Process Group ID (setpgid):
             * First process creates process group (pgid = its own pid).
             * Subsequent processes in pipeline join that group.
             */
            pid_t child_pid = getpid();
            pid_t pgid = (i == 0) ? child_pid : job->pgid;
            setpgid(child_pid, pgid);

            /* Foreground jobs take terminal control */
            if (!pipeline->background) {
                terminal_give_to(pgid);
            }

            /* Restore default signals (SIG_DFL) and unblock */
            signals_restore_defaults();

            /*
             * 2. Configure IPC Pipes (dup2):
             * If not the first command, redirect stdin from pipe[i-1][0].
             * If not the last command, redirect stdout to pipe[i][1].
             */
            if (i > 0) {
                if (dup2(pipefds[i - 1][0], STDIN_FILENO) < 0) {
                    shell_perror("dup2 (stdin pipe)");
                    _exit(EXIT_FAILURE);
                }
            }
            if (i < num_cmds - 1) {
                if (dup2(pipefds[i][1], STDOUT_FILENO) < 0) {
                    shell_perror("dup2 (stdout pipe)");
                    _exit(EXIT_FAILURE);
                }
            }

            /* Close all pipe file descriptors in child to prevent hangs/leaks */
            if (pipefds) {
                for (int j = 0; j < num_cmds - 1; j++) {
                    close(pipefds[j][0]);
                    close(pipefds[j][1]);
                }
            }

            /*
             * 3. Configure File Redirections (open, dup2, close):
             * Explicit redirections override pipe connections.
             */
            Command *cmd = &pipeline->commands[i];

            if (cmd->input_file) {
                int fd = open(cmd->input_file, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "mysh: %s: %s\n", cmd->input_file, strerror(errno));
                    _exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (cmd->output_file) {
                int flags = O_WRONLY | O_CREAT | (cmd->append_output ? O_APPEND : O_TRUNC);
                int fd = open(cmd->output_file, flags, 0644);
                if (fd < 0) {
                    fprintf(stderr, "mysh: %s: %s\n", cmd->output_file, strerror(errno));
                    _exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            if (cmd->error_file) {
                int fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    fprintf(stderr, "mysh: %s: %s\n", cmd->error_file, strerror(errno));
                    _exit(EXIT_FAILURE);
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            /* Execute built-in in child if piped (e.g. 'pwd | wc') */
            if (is_builtin(cmd->argv[0])) {
                int bstatus = execute_builtin(cmd);
                _exit(bstatus);
            }

            /*
             * 4. Program Overlay (execvp):
             * Searches PATH for binary and replaces child image.
             */
            execvp(cmd->argv[0], cmd->argv);

            /* If execvp returns, an error occurred */
            if (errno == ENOENT) {
                fprintf(stderr, "mysh: command not found: %s\n", cmd->argv[0]);
                _exit(127);
            } else {
                fprintf(stderr, "mysh: %s: %s\n", cmd->argv[0], strerror(errno));
                _exit(126);
            }
        }

        /* ==================================================== */
        /* PARENT PROCESS SETUP                                 */
        /* ==================================================== */

        /*
         * Critical Race-Condition Prevention:
         * setpgid is called in BOTH parent and child.
         * The first child sets job->pgid; parent registers it immediately.
         */
        if (i == 0) {
            job->pgid = pid;
        }
        setpgid(pid, job->pgid);

        gui_event_emit("PROC_NEW|%d|%d|%s", (int)pid, (int)job->pgid, pipeline->commands[i].argv[0]);

        /* Add process record to the job */
        job_add_process(job, pid);
    }

    /*
     * Close all pipe file descriptors in the parent process.
     * Essential: If parent keeps write ends open, reading children will never see EOF!
     */
    if (pipefds) {
        for (int j = 0; j < num_cmds - 1; j++) {
            close(pipefds[j][0]);
            close(pipefds[j][1]);
        }
        free(pipefds);
    }

    if (pipeline->background) {
        /* Background job: display [job_id] pgid and return immediately to prompt */
        printf("[%d] %d\n", job->job_id, job->pgid);
        fflush(stdout);
        g_last_exit_status = 0;
        return 0;
    } else {
        /* Foreground job: wait for completion or stop */
        ret = wait_for_job(job);
        g_last_exit_status = ret;
        return ret;
    }
}
