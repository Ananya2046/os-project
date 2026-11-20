/**
 * main.c - Entry point and REPL loop for Mini Linux Shell
 * 
 * OS Concept: The Unix Shell Architecture & Read-Eval-Print Loop (REPL)
 * Implements:
 * 1. Terminal initialization and job-control session setup.
 * 2. Signal masking and asynchronous background job notification.
 * 3. Command line reading, tokenization, pipeline execution, and cleanup.
 * 4. Clean EOF (Ctrl+D) and Ctrl+C interrupt handling.
 * 5. Sequential command separation via semicolon (;).
 * 6. Non-blocking GUI event channel initialization via --gui-fifo.
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#include "utils.h"
#include "parser.h"
#include "executor.h"
#include "builtins.h"
#include "jobs.h"
#include "signals.h"
#include "gui_events.h"

/**
 * Splits and executes sequential commands separated by unquoted semicolons (;).
 */
static void execute_command_string(char *line) {
    char *p = line;
    char *start = line;
    int in_single_quote = 0;
    int in_double_quote = 0;

    while (*p) {
        if (*p == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (*p == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (*p == ';' && !in_single_quote && !in_double_quote) {
            *p = '\0';
            char *trimmed = trim_whitespace(start);
            if (trimmed && *trimmed != '\0') {
                Pipeline *pipeline = parse_pipeline(trimmed);
                if (pipeline) {
                    execute_pipeline(pipeline);
                    free_pipeline(pipeline);
                }
            }
            start = p + 1;
        }
        p++;
    }

    char *trimmed = trim_whitespace(start);
    if (trimmed && *trimmed != '\0') {
        Pipeline *pipeline = parse_pipeline(trimmed);
        if (pipeline) {
            execute_pipeline(pipeline);
            free_pipeline(pipeline);
        }
    }
}

int main(int argc, char **argv) {
    /* Check for GUI FIFO option */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gui-fifo") == 0 && i + 1 < argc) {
            gui_events_init(argv[i + 1]);
            i++;
        }
    }

    /* Set up terminal job control and process group */
    shell_init_job_control();

    /* Configure signal handling dispositions (ignore signals in shell, handle SIGCHLD) */
    signals_init();

    /* Initialize job control table */
    jobs_init();

    char *line = NULL;
    size_t line_cap = 0;

    while (1) {
        /*
         * Asynchronous Notification:
         * Before prompting the user, check the job table for background
         * jobs that completed or stopped, notify the user, and clean them up.
         */
        job_notify_and_cleanup();

        /* Display prompt in interactive mode */
        if (shell_is_interactive) {
            printf("mysh> ");
            fflush(stdout);
        }

        /* Read user input */
        ssize_t nread = getline(&line, &line_cap, stdin);

        if (nread == -1) {
            if (feof(stdin)) {
                /* Clean EOF (Ctrl+D) received */
                if (shell_is_interactive) {
                    printf("\n");
                }
                break;
            }
            if (errno == EINTR) {
                /* Interrupted by SIGINT (Ctrl+C at prompt) */
                clearerr(stdin);
                if (shell_is_interactive) {
                    printf("\n");
                }
                continue;
            }
            shell_perror("getline");
            break;
        }

        /* Strip trailing newlines */
        while (nread > 0 && (line[nread - 1] == '\n' || line[nread - 1] == '\r')) {
            line[nread - 1] = '\0';
            nread--;
        }

        /* Resolve any literal backspaces or DEL characters in line */
        resolve_backspaces(line);

        /* Check for empty or whitespace-only lines */
        char *trimmed = trim_whitespace(line);
        if (!trimmed || *trimmed == '\0') {
            continue;
        }

        /* Save valid command line into session history */
        history_add(trimmed);

        /* Execute command line (handling semicolon separators) */
        execute_command_string(line);
    }

    /* Clean up resources on exit */
    if (line) {
        free(line);
        line = NULL;
    }

    jobs_cleanup();
    history_cleanup();
    gui_events_cleanup();

    return EXIT_SUCCESS;
}
