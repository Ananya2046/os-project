/**
 * jobs.c - Implementation of job control and process group management
 * 
 * OS Concept: Process Groups, Job State Transitions & Synchronization
 * Implements POSIX job control table:
 * - Tracking background and stopped process groups
 * - Identifying current (+) and previous (-) jobs
 * - Safe updates in the presence of asynchronous signals
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#include "jobs.h"
#include "signals.h"
#include "gui_events.h"
#include <sys/wait.h>
#include <signal.h>

static Job *g_job_list = NULL;
static int current_job_id = 0;
static int previous_job_id = 0;

static void update_current_job(int job_id) {
    if (job_id <= 0) return;
    if (current_job_id == job_id) return;
    previous_job_id = current_job_id;
    current_job_id = job_id;
}

static void on_job_removed(int job_id) {
    if (current_job_id == job_id) {
        current_job_id = previous_job_id;
        previous_job_id = 0;
        for (Job *j = g_job_list; j != NULL; j = j->next) {
            if (j->job_id != current_job_id) {
                previous_job_id = j->job_id;
                break;
            }
        }
    } else if (previous_job_id == job_id) {
        previous_job_id = 0;
        for (Job *j = g_job_list; j != NULL; j = j->next) {
            if (j->job_id != current_job_id) {
                previous_job_id = j->job_id;
                break;
            }
        }
    }
}

static int get_next_job_id(void) {
    int id = 1;
    while (1) {
        int used = 0;
        for (Job *j = g_job_list; j != NULL; j = j->next) {
            if (j->job_id == id) {
                used = 1;
                break;
            }
        }
        if (!used) return id;
        id++;
    }
}

void jobs_init(void) {
    g_job_list = NULL;
    current_job_id = 0;
    previous_job_id = 0;
}

void jobs_cleanup(void) {
    sigset_t old_mask;
    signals_block_chld(&old_mask);

    Job *j = g_job_list;
    while (j) {
        Job *next = j->next;
        Process *p = j->first_process;
        while (p) {
            Process *p_next = p->next;
            free(p);
            p = p_next;
        }
        if (j->command_line) {
            free(j->command_line);
            j->command_line = NULL;
        }
        free(j);
        j = next;
    }
    g_job_list = NULL;
    current_job_id = 0;
    previous_job_id = 0;

    signals_unblock_chld(&old_mask);
}

Job *job_create(pid_t pgid, const char *cmdline, int is_foreground) {
    sigset_t old_mask;
    signals_block_chld(&old_mask);

    Job *job = safe_malloc(sizeof(Job));
    job->job_id = get_next_job_id();
    job->pgid = pgid;
    job->command_line = safe_strdup(cmdline ? cmdline : "");
    job->first_process = NULL;
    job->num_processes = 0;
    job->state = JOB_RUNNING;
    job->is_foreground = is_foreground;
    job->notified = 0;

    /* Add to head of job list */
    job->next = g_job_list;
    g_job_list = job;

    if (!is_foreground) {
        update_current_job(job->job_id);
    }

    if (job->pgid > 0) {
        gui_event_emit("JOB_ADD|%d|%d|%s|%s", job->job_id, (int)job->pgid, job->command_line, "RUNNING");
    }

    signals_unblock_chld(&old_mask);
    return job;
}

void job_add_process(Job *job, pid_t pid) {
    if (!job) return;

    sigset_t old_mask;
    signals_block_chld(&old_mask);

    Process *proc = safe_malloc(sizeof(Process));
    proc->pid = pid;
    proc->status = 0;
    proc->completed = 0;
    proc->stopped = 0;
    proc->next = NULL;

    if (!job->first_process) {
        job->first_process = proc;
    } else {
        Process *curr = job->first_process;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = proc;
    }
    job->num_processes++;

    signals_unblock_chld(&old_mask);
}

Job *job_find_by_id(int job_id) {
    for (Job *j = g_job_list; j != NULL; j = j->next) {
        if (j->job_id == job_id) {
            return j;
        }
    }
    return NULL;
}

Job *job_find_by_pgid(pid_t pgid) {
    for (Job *j = g_job_list; j != NULL; j = j->next) {
        if (j->pgid == pgid) {
            return j;
        }
    }
    return NULL;
}

Job *job_find_by_pid(pid_t pid) {
    for (Job *j = g_job_list; j != NULL; j = j->next) {
        for (Process *p = j->first_process; p != NULL; p = p->next) {
            if (p->pid == pid) {
                return j;
            }
        }
    }
    return NULL;
}

int job_is_completed(const Job *job) {
    if (!job || !job->first_process) return 1;
    for (Process *p = job->first_process; p != NULL; p = p->next) {
        if (!p->completed) {
            return 0;
        }
    }
    return 1;
}

int job_is_stopped(const Job *job) {
    if (!job || !job->first_process) return 0;
    int has_active = 0;
    for (Process *p = job->first_process; p != NULL; p = p->next) {
        if (!p->completed) {
            has_active = 1;
            if (!p->stopped) {
                return 0;
            }
        }
    }
    return has_active;
}

int job_update_process_status(pid_t pid, int status) {
    if (pid <= 0) return -1;

    for (Job *j = g_job_list; j != NULL; j = j->next) {
        for (Process *p = j->first_process; p != NULL; p = p->next) {
            if (p->pid == pid) {
                p->status = status;
                if (WIFSTOPPED(status)) {
                    p->stopped = 1;
                } else if (WIFCONTINUED(status)) {
                    p->stopped = 0;
                } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    p->completed = 1;
                    p->stopped = 0;
                }

                if (job_is_completed(j)) {
                    j->state = JOB_DONE;
                    gui_event_emit("JOB_STATE|%d|DONE", j->job_id);
                } else if (job_is_stopped(j)) {
                    j->state = JOB_STOPPED;
                    update_current_job(j->job_id);
                    gui_event_emit("JOB_STATE|%d|STOPPED", j->job_id);
                } else {
                    j->state = JOB_RUNNING;
                    gui_event_emit("JOB_STATE|%d|RUNNING", j->job_id);
                }
                return 0;
            }
        }
    }
    return -1;
}

void job_remove(Job *job) {
    if (!job) return;

    sigset_t old_mask;
    signals_block_chld(&old_mask);

    int removed_id = job->job_id;
    gui_event_emit("JOB_DONE|%d|0", removed_id);

    if (g_job_list == job) {
        g_job_list = job->next;
    } else {
        Job *curr = g_job_list;
        while (curr && curr->next != job) {
            curr = curr->next;
        }
        if (curr) {
            curr->next = job->next;
        }
    }

    Process *p = job->first_process;
    while (p) {
        Process *p_next = p->next;
        free(p);
        p = p_next;
    }
    if (job->command_line) {
        free(job->command_line);
        job->command_line = NULL;
    }
    free(job);

    on_job_removed(removed_id);

    signals_unblock_chld(&old_mask);
}

Job *job_get_current(void) {
    if (current_job_id > 0) {
        Job *j = job_find_by_id(current_job_id);
        if (j) return j;
    }
    return g_job_list;
}

Job *job_get_previous(void) {
    if (previous_job_id > 0) {
        Job *j = job_find_by_id(previous_job_id);
        if (j) return j;
    }
    if (g_job_list && g_job_list->next) {
        return g_job_list->next;
    }
    return NULL;
}

int job_count_stopped(void) {
    sigset_t old_mask;
    signals_block_chld(&old_mask);

    int count = 0;
    for (Job *j = g_job_list; j != NULL; j = j->next) {
        if (j->state == JOB_STOPPED) {
            count++;
        }
    }

    signals_unblock_chld(&old_mask);
    return count;
}

void job_kill_all_stopped(void) {
    sigset_t old_mask;
    signals_block_chld(&old_mask);

    for (Job *j = g_job_list; j != NULL; j = j->next) {
        if (j->state == JOB_STOPPED) {
            kill(-j->pgid, SIGHUP);
            kill(-j->pgid, SIGCONT);
        }
    }

    signals_unblock_chld(&old_mask);
}

static char get_job_marker(int job_id) {
    if (job_id == current_job_id) return '+';
    if (job_id == previous_job_id) return '-';
    return ' ';
}

static void get_job_status_string(const Job *job, char *buf, size_t buf_size) {
    if (job->state == JOB_RUNNING) {
        snprintf(buf, buf_size, "Running");
    } else if (job->state == JOB_STOPPED) {
        snprintf(buf, buf_size, "Stopped");
    } else {
        /* Check if any process terminated due to a signal */
        int signaled = 0;
        int sig_num = 0;
        for (Process *p = job->first_process; p != NULL; p = p->next) {
            if (WIFSIGNALED(p->status)) {
                signaled = 1;
                sig_num = WTERMSIG(p->status);
                break;
            }
        }
        if (signaled) {
            snprintf(buf, buf_size, "%s", strsignal(sig_num));
        } else {
            snprintf(buf, buf_size, "Done");
        }
    }
}

void job_print_list(void) {
    sigset_t old_mask;
    signals_block_chld(&old_mask);

    for (Job *j = g_job_list; j != NULL; j = j->next) {
        char marker = get_job_marker(j->job_id);
        char status_str[64];
        get_job_status_string(j, status_str, sizeof(status_str));
        printf("[%d]%c  %-20s %s\n", j->job_id, marker, status_str, j->command_line);
    }
    fflush(stdout);

    signals_unblock_chld(&old_mask);
}

void job_notify_and_cleanup(void) {
    sigset_t old_mask;
    signals_block_chld(&old_mask);

    Job *curr = g_job_list;
    while (curr) {
        Job *next = curr->next;

        if (job_is_completed(curr)) {
            char marker = get_job_marker(curr->job_id);
            char status_str[64];
            get_job_status_string(curr, status_str, sizeof(status_str));
            printf("[%d]%c  %-20s %s\n", curr->job_id, marker, status_str, curr->command_line);
            fflush(stdout);
            job_remove(curr);
        } else if (curr->state == JOB_STOPPED && !curr->notified) {
            char marker = get_job_marker(curr->job_id);
            printf("[%d]%c  %-20s %s\n", curr->job_id, marker, "Stopped", curr->command_line);
            fflush(stdout);
            curr->notified = 1;
        }

        curr = next;
    }

    signals_unblock_chld(&old_mask);
}

