/**
 * parser.h - Command and pipeline parsing interface for Mini Linux Shell
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#ifndef PARSER_H
#define PARSER_H

#include "utils.h"

/**
 * Command struct represents a single command within a pipeline.
 * e.g., in "cat < input.txt | grep foo > output.txt"
 * Command 1: argv = ["cat", NULL], input_file = "input.txt", output_file = NULL
 * Command 2: argv = ["grep", "foo", NULL], input_file = NULL, output_file = "output.txt"
 */
typedef struct Command {
    char **argv;        /* NULL-terminated argument array for execvp */
    int argc;           /* Argument count */
    char *input_file;   /* File for stdin redirection (<), or NULL */
    char *output_file;  /* File for stdout redirection (> or >>), or NULL */
    int append_output;  /* 1 if >> (append), 0 if > (truncate) */
    char *error_file;   /* File for stderr redirection (2>), or NULL */
} Command;

/**
 * Pipeline struct represents a complete pipeline of one or more commands.
 * e.g., "cmd1 | cmd2 | cmd3 &"
 */
typedef struct Pipeline {
    Command *commands;  /* Array of Command structures */
    int num_commands;   /* Number of commands in the pipeline */
    int background;     /* 1 if command line ends with '&', 0 otherwise */
    char *raw_input;    /* Copy of raw input line for job description */
} Pipeline;

/**
 * Parses a raw command line string into a Pipeline struct.
 * Handles single/double quotes, pipes (|), input (<), output (> and >>),
 * error redirection (2>), and background (&).
 * 
 * Returns a dynamically allocated Pipeline on success, or NULL on empty line/syntax error.
 */
Pipeline *parse_pipeline(const char *line);

/**
 * Frees a Command and its allocated arguments and file paths.
 */
void free_command(Command *cmd);

/**
 * Frees a Pipeline and all its contained commands.
 */
void free_pipeline(Pipeline *pipeline);

#endif /* PARSER_H */

