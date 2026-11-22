/**
 * builtins.c - Implementation of shell built-in commands
 * 
 * OS Concept: Shell Built-ins vs External Programs
 * Built-ins execute in the parent process because they modify the parent's
 * environment (e.g., current directory with 'cd', environment variables with 'export',
 * or process-group state with 'fg'/'bg'). Forking a child would cause changes
 * to be lost upon child termination.
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#include "builtins.h"
#include "jobs.h"
#include "signals.h"
#include "executor.h"
#include "gui_events.h"
#include <limits.h>
#include <signal.h>
#include <strings.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

extern char **environ;

int g_exit_warned = 0;
int g_last_exit_status = 0;

/* History list structure */
typedef struct HistoryNode {
    char *cmd;
    struct HistoryNode *next;
} HistoryNode;

static HistoryNode *g_history_head = NULL;
static HistoryNode *g_history_tail = NULL;
static int g_history_count = 0;

void history_add(const char *line) {
    if (!line || *line == '\0') return;

    HistoryNode *node = safe_malloc(sizeof(HistoryNode));
    node->cmd = safe_strdup(line);
    node->next = NULL;

    if (!g_history_head) {
        g_history_head = node;
        g_history_tail = node;
    } else {
        g_history_tail->next = node;
        g_history_tail = node;
    }
    g_history_count++;
}

void history_print(void) {
    int idx = 1;
    for (HistoryNode *curr = g_history_head; curr != NULL; curr = curr->next) {
        printf("%5d  %s\n", idx++, curr->cmd);
    }
}

void history_cleanup(void) {
    HistoryNode *curr = g_history_head;
    while (curr) {
        HistoryNode *next = curr->next;
        free(curr->cmd);
        free(curr);
        curr = next;
    }
    g_history_head = NULL;
    g_history_tail = NULL;
    g_history_count = 0;
}

int is_builtin(const char *cmd_name) {
    if (!cmd_name) return 0;
    return (strcmp(cmd_name, "cd") == 0 ||
            strcmp(cmd_name, "pwd") == 0 ||
            strcmp(cmd_name, "echo") == 0 ||
            strcmp(cmd_name, "exit") == 0 ||
            strcmp(cmd_name, "jobs") == 0 ||
            strcmp(cmd_name, "fg") == 0 ||
            strcmp(cmd_name, "bg") == 0 ||
            strcmp(cmd_name, "kill") == 0 ||
            strcmp(cmd_name, "clear") == 0 ||
            strcmp(cmd_name, "export") == 0 ||
            strcmp(cmd_name, "help") == 0 ||
            strcmp(cmd_name, "history") == 0);
}

static int builtin_cd(Command *cmd) {
    const char *target = NULL;
    char path_buf[PATH_MAX];
    char cwd_before[PATH_MAX];
    char cwd_after[PATH_MAX];

    if (getcwd(cwd_before, sizeof(cwd_before)) == NULL) {
        const char *pwd_env = getenv("PWD");
        if (pwd_env) {
            strncpy(cwd_before, pwd_env, sizeof(cwd_before) - 1);
            cwd_before[sizeof(cwd_before) - 1] = '\0';
        } else {
            cwd_before[0] = '\0';
        }
    }

    if (cmd->argc < 2) {
        target = getenv("HOME");
        if (!target) {
            shell_error("cd: HOME not set");
            return 1;
        }
    } else if (strcmp(cmd->argv[1], "-") == 0) {
        target = getenv("OLDPWD");
        if (!target) {
            shell_error("cd: OLDPWD not set");
            return 1;
        }
        printf("%s\n", target);
    } else if (cmd->argv[1][0] == '~') {
        const char *home = getenv("HOME");
        if (!home) home = "";
        if (cmd->argv[1][1] == '\0') {
            target = home;
        } else if (cmd->argv[1][1] == '/') {
            snprintf(path_buf, sizeof(path_buf), "%s%s", home, cmd->argv[1] + 1);
            target = path_buf;
        } else {
            target = cmd->argv[1];
        }
    } else {
        target = cmd->argv[1];
    }

    if (chdir(target) != 0) {
        shell_perror("cd");
        return 1;
    }

    if (getcwd(cwd_after, sizeof(cwd_after)) != NULL) {
        if (cwd_before[0] != '\0') {
            setenv("OLDPWD", cwd_before, 1);
        }
        setenv("PWD", cwd_after, 1);
        gui_event_emit("CWD_CHANGE|%s", cwd_after);
    } else {
        if (cwd_before[0] != '\0') {
            setenv("OLDPWD", cwd_before, 1);
        }
        setenv("PWD", target, 1);
        gui_event_emit("CWD_CHANGE|%s", target);
    }

    return 0;
}

static int builtin_pwd(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
        return 0;
    }
    const char *pwd_env = getenv("PWD");
    if (pwd_env && *pwd_env != '\0') {
        printf("%s\n", pwd_env);
        return 0;
    }
    shell_perror("pwd");
    return 1;
}

static int builtin_echo(Command *cmd) {
    int start = 1;
    int newline = 1;

    if (cmd->argc > 1 && strcmp(cmd->argv[1], "-n") == 0) {
        newline = 0;
        start = 2;
    }

    for (int i = start; i < cmd->argc; i++) {
        printf("%s%s", cmd->argv[i], (i == cmd->argc - 1) ? "" : " ");
    }
    if (newline) {
        printf("\n");
    }
    fflush(stdout);
    return 0;
}

static int builtin_clear(void) {
    printf("\033[H\033[2J");
    fflush(stdout);
    return 0;
}

static int builtin_export(Command *cmd) {
    if (cmd->argc < 2) {
        for (char **e = environ; *e != NULL; e++) {
            printf("%s\n", *e);
        }
        return 0;
    }

    for (int i = 1; i < cmd->argc; i++) {
        char *arg = cmd->argv[i];
        char *eq = strchr(arg, '=');
        if (eq) {
            *eq = '\0';
            setenv(arg, eq + 1, 1);
            *eq = '=';
        }
    }
    return 0;
}

static int builtin_exit(Command *cmd) {
    int stopped_count = job_count_stopped();
    if (stopped_count > 0 && !g_exit_warned) {
        printf("There are stopped jobs.\n");
        g_exit_warned = 1;
        return 1;
    }

    if (stopped_count > 0) {
        job_kill_all_stopped();
    }

    int exit_code = 0;
    if (cmd->argc > 1) {
        exit_code = atoi(cmd->argv[1]);
    }

    jobs_cleanup();
    history_cleanup();
    exit(exit_code);
}

static int builtin_jobs(void) {
    job_print_list();
    return 0;
}

static Job *parse_job_spec(Command *cmd, const char *builtin_name) {
    if (cmd->argc < 2) {
        Job *j = job_get_current();
        if (!j) {
            fprintf(stderr, "mysh: %s: current: no such job\n", builtin_name);
            return NULL;
        }
        return j;
    }

    const char *arg = cmd->argv[1];
    if (*arg == '%') {
        arg++;
    }

    int job_id = atoi(arg);
    if (job_id <= 0) {
        fprintf(stderr, "mysh: %s: %s: no such job\n", builtin_name, cmd->argv[1]);
        return NULL;
    }

    Job *j = job_find_by_id(job_id);
    if (!j) {
        fprintf(stderr, "mysh: %s: %s: no such job\n", builtin_name, cmd->argv[1]);
        return NULL;
    }
    return j;
}

static int builtin_fg(Command *cmd) {
    Job *job = parse_job_spec(cmd, "fg");
    if (!job) return 1;

    printf("%s\n", job->command_line);
    fflush(stdout);

    /* Send SIGCONT to the whole process group if stopped */
    if (job->state == JOB_STOPPED && job->pgid > 0) {
        if (kill(-job->pgid, SIGCONT) < 0) {
            shell_perror("fg: kill (SIGCONT)");
        }
        gui_event_emit("SIG_EVENT|SIGCONT|Sent SIGCONT to pgid %d (fg)", (int)job->pgid);
        job->state = JOB_RUNNING;
    }

    job->is_foreground = 1;
    return wait_for_job(job);
}

static int builtin_bg(Command *cmd) {
    Job *job = parse_job_spec(cmd, "bg");
    if (!job) return 1;

    if (job->pgid > 0 && kill(-job->pgid, SIGCONT) < 0) {
        shell_perror("bg: kill (SIGCONT)");
        return 1;
    }
    gui_event_emit("SIG_EVENT|SIGCONT|Sent SIGCONT to pgid %d (bg)", (int)job->pgid);

    job->state = JOB_RUNNING;
    job->is_foreground = 0;
    job->notified = 0;

    printf("[%d]+ %s &\n", job->job_id, job->command_line);
    fflush(stdout);
    return 0;
}

static int builtin_kill(Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "mysh: kill: usage: kill [-sig] pid | %%job_id ...\n");
        return 1;
    }

    int sig = SIGTERM;
    int arg_idx = 1;

    /* Parse optional signal flag: -9, -15, -TERM, -KILL, -STOP, -CONT */
    if (cmd->argv[arg_idx][0] == '-' && cmd->argv[arg_idx][1] != '\0') {
        const char *s = cmd->argv[arg_idx] + 1;
        if (strcmp(s, "9") == 0 || strcasecmp(s, "KILL") == 0 || strcasecmp(s, "SIGKILL") == 0) {
            sig = SIGKILL;
        } else if (strcmp(s, "15") == 0 || strcasecmp(s, "TERM") == 0 || strcasecmp(s, "SIGTERM") == 0) {
            sig = SIGTERM;
        } else if (strcmp(s, "2") == 0 || strcasecmp(s, "INT") == 0 || strcasecmp(s, "SIGINT") == 0) {
            sig = SIGINT;
        } else if (strcmp(s, "19") == 0 || strcasecmp(s, "STOP") == 0 || strcasecmp(s, "SIGSTOP") == 0) {
            sig = SIGSTOP;
        } else if (strcmp(s, "18") == 0 || strcasecmp(s, "CONT") == 0 || strcasecmp(s, "SIGCONT") == 0) {
            sig = SIGCONT;
        } else {
            sig = atoi(s);
            if (sig <= 0) sig = SIGTERM;
        }
        arg_idx++;
    }

    int ret = 0;
    for (int i = arg_idx; i < cmd->argc; i++) {
        const char *target = cmd->argv[i];
        if (target[0] == '%') {
            int jid = atoi(target + 1);
            Job *j = job_find_by_id(jid);
            if (!j) {
                fprintf(stderr, "mysh: kill: %s: no such job\n", target);
                ret = 1;
                continue;
            }
            if (j->pgid > 0) {
                if (kill(-j->pgid, sig) < 0) {
                    shell_perror("kill");
                    ret = 1;
                } else {
                    gui_event_emit("SIG_EVENT|SIGKILL|Sent signal %d to job [%d] (pgid %d)", sig, jid, (int)j->pgid);
                }
            }
        } else {
            pid_t pid = (pid_t)atoi(target);
            if (pid == 0) {
                fprintf(stderr, "mysh: kill: %s: invalid pid\n", target);
                ret = 1;
                continue;
            }
            if (kill(pid, sig) < 0) {
                shell_perror("kill");
                ret = 1;
            } else {
                gui_event_emit("SIG_EVENT|SIGKILL|Sent signal %d to pid %d", sig, (int)pid);
            }
        }
    }
    return ret;
}

static int builtin_help(void) {
    printf("Mini Linux Shell (mysh) - Built-in Commands & Features\n");
    printf("=====================================================\n");
    printf("Built-in Commands:\n");
    printf("  cd [dir]       Change current working directory (supports HOME, '~', and '-')\n");
    printf("  pwd            Print current working directory\n");
    printf("  echo [args]    Print arguments to standard output (-n supported)\n");
    printf("  clear          Clear terminal display screen\n");
    printf("  export VAR=val Set environment variable in shell environment\n");
    printf("  jobs           List active and stopped jobs with [+] and [-] markers\n");
    printf("  fg [%%job_id]   Bring background or stopped job to the foreground\n");
    printf("  bg [%%job_id]   Resume stopped job in the background (send SIGCONT)\n");
    printf("  kill [-sig] id Send signal to process or %%job_id (e.g. kill -9 %%1)\n");
    printf("  history        Display command history for this session\n");
    printf("  help           Display this help information\n");
    printf("  exit [code]    Exit the shell (warns if stopped jobs exist)\n\n");
    printf("Features Supported:\n");
    printf("  I/O Redirection : < input, > output (truncate), >> output (append), 2> stderr\n");
    printf("  Pipelines       : cmd1 | cmd2 | cmd3 ...\n");
    printf("  Background Jobs : cmd &\n");
    printf("  Command Chain   : cmd1 ; cmd2 (sequential execution)\n");
    printf("  Variables       : $VAR, $?, $$, $HOME, etc.\n");
    printf("  Wildcards       : *.c, *.h, * (expanded via glob)\n");
    printf("  Job Control     : Ctrl+C (SIGINT foreground), Ctrl+Z (SIGTSTP foreground)\n");
    return 0;
}

int execute_builtin(Command *cmd) {
    if (!cmd || cmd->argc == 0) return 0;

    const char *name = cmd->argv[0];

    /* Any command other than exit clears the exit warning */
    if (strcmp(name, "exit") != 0) {
        g_exit_warned = 0;
    }

    if (strcmp(name, "cd") == 0) {
        return builtin_cd(cmd);
    } else if (strcmp(name, "pwd") == 0) {
        return builtin_pwd();
    } else if (strcmp(name, "echo") == 0) {
        return builtin_echo(cmd);
    } else if (strcmp(name, "clear") == 0) {
        return builtin_clear();
    } else if (strcmp(name, "export") == 0) {
        return builtin_export(cmd);
    } else if (strcmp(name, "exit") == 0) {
        return builtin_exit(cmd);
    } else if (strcmp(name, "jobs") == 0) {
        return builtin_jobs();
    } else if (strcmp(name, "fg") == 0) {
        return builtin_fg(cmd);
    } else if (strcmp(name, "bg") == 0) {
        return builtin_bg(cmd);
    } else if (strcmp(name, "kill") == 0) {
        return builtin_kill(cmd);
    } else if (strcmp(name, "help") == 0) {
        return builtin_help();
    } else if (strcmp(name, "history") == 0) {
        history_print();
        return 0;
    }

    return 1;
}
