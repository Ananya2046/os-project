/**
 * builtins.h - Shell built-in commands interface
 * 
 * OS Concept: Shell Built-ins vs. External Programs
 * Explains why commands like 'cd', 'export', 'exit', 'jobs', 'fg', and 'bg' MUST be
 * executed directly by the parent shell process without fork().
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include "parser.h"

/* Flag tracking consecutive exit attempts when stopped jobs exist */
extern int g_exit_warned;

/* Last command exit status ($?) */
extern int g_last_exit_status;

/**
 * Checks if the given command name is a shell built-in.
 * Returns 1 if built-in, 0 otherwise.
 */
int is_builtin(const char *cmd_name);

/**
 * Executes a built-in command directly in the shell process.
 * Returns the command's exit status (0 for success, non-zero on failure).
 */
int execute_builtin(Command *cmd);

/**
 * Adds a command line to shell execution history.
 */
void history_add(const char *line);

/**
 * Prints the recorded command history.
 */
void history_print(void);

/**
 * Cleans up history records.
 */
void history_cleanup(void);

#endif /* BUILTINS_H */
