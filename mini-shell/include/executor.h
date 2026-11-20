/**
 * executor.h - Command execution and pipeline orchestration interface
 * 
 * OS Concept: Process Creation (fork), Program Execution (execvp),
 * Inter-Process Communication (pipe), and File Descriptor Duplication (dup2).
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"
#include "jobs.h"

/**
 * Executes a parsed pipeline.
 * Handles:
 * 1. Single built-in commands (executed directly in parent shell).
 * 2. External commands and multi-stage pipelines (fork, pipe, dup2, execvp).
 * 3. Foreground terminal assignment and waiting vs. background job tracking.
 * 
 * Returns the exit status of the pipeline (or 0 for background jobs).
 */
int execute_pipeline(Pipeline *pipeline);

/**
 * Puts a job into the foreground, transfers terminal control,
 * and waits until all processes in the job complete or stop.
 * 
 * Returns the exit status of the last process in the job.
 */
int wait_for_job(Job *job);

#endif /* EXECUTOR_H */

