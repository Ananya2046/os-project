/**
 * jobs.h - Job control and process table management for Mini Linux Shell
 * 
 * OS Concept: Process Groups, Sessions, and Job Control
 * Manages active pipelines as jobs, tracking process group IDs,
 * execution states (RUNNING, STOPPED, DONE), and terminal association.
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#ifndef JOBS_H
#define JOBS_H

#include "utils.h"
#include <sys/types.h>

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

/**
 * Process representation within a Job pipeline.
 */
typedef struct Process {
    pid_t pid;
    int status;             /* Exit status or signal code */
    int completed;          /* 1 if terminated, 0 if still running/stopped */
    int stopped;            /* 1 if currently stopped (SIGTSTP/SIGSTOP) */
    struct Process *next;
} Process;

/**
 * Job representation tracking an entire pipeline in its own process group.
 */
typedef struct Job {
    int job_id;             /* 1-based job identifier [1], [2], ... */
    pid_t pgid;             /* Process Group ID of the job */
    char *command_line;     /* Display string of the command */
    Process *first_process; /* Linked list of processes in the pipeline */
    int num_processes;      /* Total number of processes in the pipeline */
    JobState state;         /* RUNNING, STOPPED, or DONE */
    int is_foreground;      /* 1 if foreground job, 0 if background */
    int notified;           /* 1 if user has been notified of state change */
    struct Job *next;
} Job;

/**
 * Initializes the job control subsystem.
 */
void jobs_init(void);

/**
 * Cleans up and frees all jobs and allocated resources.
 */
void jobs_cleanup(void);

/**
 * Creates and registers a new job in the job table.
 * Returns the created Job pointer.
 */
Job *job_create(pid_t pgid, const char *cmdline, int is_foreground);

/**
 * Adds a process PID to a job's process list.
 */
void job_add_process(Job *job, pid_t pid);

/**
 * Finds a job by its 1-based job_id.
 */
Job *job_find_by_id(int job_id);

/**
 * Finds a job by its process group ID.
 */
Job *job_find_by_pgid(pid_t pgid);

/**
 * Finds a job that contains a given process PID.
 */
Job *job_find_by_pid(pid_t pid);

/**
 * Updates status of a process within the job table.
 * Called when waitpid() reports an event for `pid`.
 */
int job_update_process_status(pid_t pid, int status);

/**
 * Returns 1 if all processes in the job have completed.
 */
int job_is_completed(const Job *job);

/**
 * Returns 1 if all non-completed processes in the job are stopped.
 */
int job_is_stopped(const Job *job);

/**
 * Removes a job from the job table and frees its resources.
 */
void job_remove(Job *job);

/**
 * Checks for completed or stopped background jobs, prints notifications
 * (e.g. "[1]+ Done cmd &"), and cleans up completed jobs.
 * Called before printing each shell prompt.
 */
void job_notify_and_cleanup(void);

/**
 * Implements the built-in 'jobs' command:
 * Prints all active (running or stopped) jobs with [+] and [-] markers.
 */
void job_print_list(void);

/**
 * Returns the "current" job marked with '+'.
 */
Job *job_get_current(void);

/**
 * Returns the "previous" job marked with '-'.
 */
Job *job_get_previous(void);

/**
 * Returns the count of jobs currently in STOPPED state.
 */
int job_count_stopped(void);

/**
 * Sends SIGHUP followed by SIGCONT to all stopped jobs on shell exit.
 */
void job_kill_all_stopped(void);

#endif /* JOBS_H */

