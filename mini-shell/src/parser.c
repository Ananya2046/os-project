/**
 * parser.c - Implementation of lexical analysis and command parsing
 * 
 * OS Concept: Lexical Analysis & Abstract Syntax Tree (AST) creation
 * Parses user input lines into structured pipelines and commands,
 * isolating parsing logic from execution.
 * 
 * Features:
 * - Single quotes ('...') and Double quotes ("...")
 * - Variable expansion ($VAR, $?, $$)
 * - Wildcard expansion (*, ?) via glob()
 * - Operators: |, <, >, >>, 2>, &
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#include "parser.h"
#include "builtins.h"
#include <ctype.h>
#include <glob.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,            /* | */
    TOKEN_REDIRECT_IN,     /* < */
    TOKEN_REDIRECT_OUT,    /* > */
    TOKEN_REDIRECT_APPEND, /* >> */
    TOKEN_REDIRECT_ERR,    /* 2> */
    TOKEN_BACKGROUND,      /* & */
    TOKEN_EOF,
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    int was_quoted;
} Token;

typedef struct {
    Token *tokens;
    int count;
    int capacity;
} TokenList;

static void token_list_init(TokenList *tl) {
    tl->capacity = 16;
    tl->count = 0;
    tl->tokens = safe_malloc(sizeof(Token) * tl->capacity);
}

static void token_list_add(TokenList *tl, TokenType type, const char *val, int was_quoted) {
    if (tl->count >= tl->capacity) {
        tl->capacity *= 2;
        tl->tokens = safe_realloc(tl->tokens, sizeof(Token) * tl->capacity);
    }
    tl->tokens[tl->count].type = type;
    tl->tokens[tl->count].value = val ? safe_strdup(val) : NULL;
    tl->tokens[tl->count].was_quoted = was_quoted;
    tl->count++;
}

static void token_list_free(TokenList *tl) {
    if (!tl) return;
    for (int i = 0; i < tl->count; i++) {
        if (tl->tokens[i].value) {
            free(tl->tokens[i].value);
            tl->tokens[i].value = NULL;
        }
    }
    free(tl->tokens);
    tl->tokens = NULL;
    tl->count = 0;
    tl->capacity = 0;
}

/**
 * Tokenizes the input line into an array of tokens.
 * Handles single quotes (''), double quotes (""), environment variables ($),
 * and operators.
 * Returns 0 on success, -1 on syntax error.
 */
static int tokenize(const char *input, TokenList *tl) {
    const char *p = input;
    char buffer[4096];

    while (*p) {
        /* Skip whitespace */
        while (*p && is_whitespace(*p)) {
            p++;
        }
        if (!*p) break;

        /* Check for 2> */
        if (*p == '2' && *(p + 1) == '>') {
            token_list_add(tl, TOKEN_REDIRECT_ERR, "2>", 0);
            p += 2;
            continue;
        }

        /* Check single-character or two-character operators */
        if (*p == '|') {
            token_list_add(tl, TOKEN_PIPE, "|", 0);
            p++;
            continue;
        }
        if (*p == '<') {
            token_list_add(tl, TOKEN_REDIRECT_IN, "<", 0);
            p++;
            continue;
        }
        if (*p == '>') {
            if (*(p + 1) == '>') {
                token_list_add(tl, TOKEN_REDIRECT_APPEND, ">>", 0);
                p += 2;
            } else {
                token_list_add(tl, TOKEN_REDIRECT_OUT, ">", 0);
                p++;
            }
            continue;
        }
        if (*p == '&') {
            token_list_add(tl, TOKEN_BACKGROUND, "&", 0);
            p++;
            continue;
        }

        /* Scan a word, handling quotes, escapes, and variable expansions */
        int buf_idx = 0;
        int error = 0;
        int word_has_quotes = 0;

        while (*p && !is_whitespace(*p)) {
            /* Check if unquoted operator begins */
            if (*p == '|' || *p == '<' || *p == '>' || *p == '&' || (*p == '2' && *(p + 1) == '>')) {
                break;
            }

            if (*p == '\'') {
                /* Single quotes: preserve everything literally without variable expansion */
                word_has_quotes = 1;
                p++;
                while (*p && *p != '\'') {
                    if (buf_idx < (int)sizeof(buffer) - 1) {
                        buffer[buf_idx++] = *p;
                    }
                    p++;
                }
                if (*p != '\'') {
                    shell_error("syntax error: unmatched single quote");
                    error = 1;
                    break;
                }
                p++; /* skip closing single quote */
            } else if (*p == '"') {
                /* Double quotes: handle escapes and variable expansion */
                word_has_quotes = 1;
                p++;
                while (*p && *p != '"') {
                    if (*p == '\\' && (*(p + 1) == '"' || *(p + 1) == '\\' || *(p + 1) == '$' || *(p + 1) == '`')) {
                        p++;
                        if (buf_idx < (int)sizeof(buffer) - 1) {
                            buffer[buf_idx++] = *p++;
                        }
                    } else if (*p == '$') {
                        /* Variable expansion inside double quotes */
                        p++;
                        if (*p == '?') {
                            char num_buf[16];
                            snprintf(num_buf, sizeof(num_buf), "%d", g_last_exit_status);
                            for (char *c = num_buf; *c && buf_idx < (int)sizeof(buffer) - 1; c++) {
                                buffer[buf_idx++] = *c;
                            }
                            p++;
                        } else if (*p == '$') {
                            char num_buf[16];
                            snprintf(num_buf, sizeof(num_buf), "%d", (int)getpid());
                            for (char *c = num_buf; *c && buf_idx < (int)sizeof(buffer) - 1; c++) {
                                buffer[buf_idx++] = *c;
                            }
                            p++;
                        } else if (isalpha((unsigned char)*p) || *p == '_') {
                            char varname[256];
                            int vlen = 0;
                            while (*p && (isalnum((unsigned char)*p) || *p == '_') && vlen < 255) {
                                varname[vlen++] = *p++;
                            }
                            varname[vlen] = '\0';
                            const char *val = getenv(varname);
                            if (val) {
                                while (*val && buf_idx < (int)sizeof(buffer) - 1) {
                                    buffer[buf_idx++] = *val++;
                                }
                            }
                        } else {
                            if (buf_idx < (int)sizeof(buffer) - 1) {
                                buffer[buf_idx++] = '$';
                            }
                        }
                    } else {
                        if (buf_idx < (int)sizeof(buffer) - 1) {
                            buffer[buf_idx++] = *p;
                        }
                        p++;
                    }
                }
                if (*p != '"') {
                    shell_error("syntax error: unmatched double quote");
                    error = 1;
                    break;
                }
                p++; /* skip closing double quote */
            } else if (*p == '$') {
                /* Variable expansion outside quotes */
                p++;
                if (*p == '?') {
                    char num_buf[16];
                    snprintf(num_buf, sizeof(num_buf), "%d", g_last_exit_status);
                    for (char *c = num_buf; *c && buf_idx < (int)sizeof(buffer) - 1; c++) {
                        buffer[buf_idx++] = *c;
                    }
                    p++;
                } else if (*p == '$') {
                    char num_buf[16];
                    snprintf(num_buf, sizeof(num_buf), "%d", (int)getpid());
                    for (char *c = num_buf; *c && buf_idx < (int)sizeof(buffer) - 1; c++) {
                        buffer[buf_idx++] = *c;
                    }
                    p++;
                } else if (isalpha((unsigned char)*p) || *p == '_') {
                    char varname[256];
                    int vlen = 0;
                    while (*p && (isalnum((unsigned char)*p) || *p == '_') && vlen < 255) {
                        varname[vlen++] = *p++;
                    }
                    varname[vlen] = '\0';
                    const char *val = getenv(varname);
                    if (val) {
                        while (*val && buf_idx < (int)sizeof(buffer) - 1) {
                            buffer[buf_idx++] = *val++;
                        }
                    }
                } else {
                    if (buf_idx < (int)sizeof(buffer) - 1) {
                        buffer[buf_idx++] = '$';
                    }
                }
            } else if (*p == '\\') {
                /* Escape next character outside quotes */
                p++;
                if (*p) {
                    if (buf_idx < (int)sizeof(buffer) - 1) {
                        buffer[buf_idx++] = *p;
                    }
                    p++;
                }
            } else {
                if (buf_idx < (int)sizeof(buffer) - 1) {
                    buffer[buf_idx++] = *p;
                }
                p++;
            }
        }

        if (error) {
            return -1;
        }

        buffer[buf_idx] = '\0';
        token_list_add(tl, TOKEN_WORD, buffer, word_has_quotes);
    }

    token_list_add(tl, TOKEN_EOF, NULL, 0);
    return 0;
}

static void cmd_init(Command *cmd) {
    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->append_output = 0;
    cmd->error_file = NULL;
}

static void cmd_add_arg(Command *cmd, const char *arg) {
    cmd->argv = safe_realloc(cmd->argv, sizeof(char *) * (cmd->argc + 2));
    cmd->argv[cmd->argc] = safe_strdup(arg);
    cmd->argc++;
    cmd->argv[cmd->argc] = NULL;
}

static void cmd_add_arg_with_glob(Command *cmd, const char *arg, int was_quoted) {
    /* Perform pathname expansion (wildcards) if unquoted and contains * or ? */
    if (!was_quoted && (strchr(arg, '*') || strchr(arg, '?'))) {
        glob_t g;
        if (glob(arg, 0, NULL, &g) == 0 && g.gl_pathc > 0) {
            for (size_t i = 0; i < g.gl_pathc; i++) {
                cmd_add_arg(cmd, g.gl_pathv[i]);
            }
            globfree(&g);
            return;
        }
        globfree(&g);
    }
    cmd_add_arg(cmd, arg);
}

void free_command(Command *cmd) {
    if (!cmd) return;
    if (cmd->argv) {
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
            cmd->argv[i] = NULL;
        }
        free(cmd->argv);
        cmd->argv = NULL;
    }
    if (cmd->input_file) {
        free(cmd->input_file);
        cmd->input_file = NULL;
    }
    if (cmd->output_file) {
        free(cmd->output_file);
        cmd->output_file = NULL;
    }
    if (cmd->error_file) {
        free(cmd->error_file);
        cmd->error_file = NULL;
    }
    cmd->argc = 0;
}

void free_pipeline(Pipeline *pipeline) {
    if (!pipeline) return;
    if (pipeline->commands) {
        for (int i = 0; i < pipeline->num_commands; i++) {
            free_command(&pipeline->commands[i]);
        }
        free(pipeline->commands);
        pipeline->commands = NULL;
    }
    if (pipeline->raw_input) {
        free(pipeline->raw_input);
        pipeline->raw_input = NULL;
    }
    free(pipeline);
}

Pipeline *parse_pipeline(const char *line) {
    if (!line) return NULL;

    TokenList tl;
    token_list_init(&tl);

    if (tokenize(line, &tl) < 0) {
        token_list_free(&tl);
        return NULL;
    }

    /* Check for empty line */
    if (tl.count == 0 || tl.tokens[0].type == TOKEN_EOF) {
        token_list_free(&tl);
        return NULL;
    }

    Pipeline *pipeline = safe_malloc(sizeof(Pipeline));
    pipeline->commands = NULL;
    pipeline->num_commands = 0;
    pipeline->background = 0;
    pipeline->raw_input = safe_strdup(line);

    int cmd_capacity = 4;
    pipeline->commands = safe_malloc(sizeof(Command) * cmd_capacity);

    Command current_cmd;
    cmd_init(&current_cmd);

    int idx = 0;
    int syntax_error = 0;

    while (idx < tl.count && tl.tokens[idx].type != TOKEN_EOF) {
        Token *t = &tl.tokens[idx];

        if (t->type == TOKEN_WORD) {
            cmd_add_arg_with_glob(&current_cmd, t->value, t->was_quoted);
            idx++;
        } else if (t->type == TOKEN_REDIRECT_IN || t->type == TOKEN_REDIRECT_OUT ||
                   t->type == TOKEN_REDIRECT_APPEND || t->type == TOKEN_REDIRECT_ERR) {
            TokenType rtype = t->type;
            idx++;
            if (idx >= tl.count || tl.tokens[idx].type != TOKEN_WORD) {
                const char *tok = (idx < tl.count && tl.tokens[idx].value) ? tl.tokens[idx].value : "newline";
                fprintf(stderr, "mysh: syntax error near unexpected token '%s'\n", tok);
                syntax_error = 1;
                break;
            }

            Token *next_t = &tl.tokens[idx];
            if (rtype == TOKEN_REDIRECT_IN) {
                if (current_cmd.input_file) free(current_cmd.input_file);
                current_cmd.input_file = safe_strdup(next_t->value);
            } else if (rtype == TOKEN_REDIRECT_OUT) {
                if (current_cmd.output_file) free(current_cmd.output_file);
                current_cmd.output_file = safe_strdup(next_t->value);
                current_cmd.append_output = 0;
            } else if (rtype == TOKEN_REDIRECT_APPEND) {
                if (current_cmd.output_file) free(current_cmd.output_file);
                current_cmd.output_file = safe_strdup(next_t->value);
                current_cmd.append_output = 1;
            } else if (rtype == TOKEN_REDIRECT_ERR) {
                if (current_cmd.error_file) free(current_cmd.error_file);
                current_cmd.error_file = safe_strdup(next_t->value);
            }
            idx++;
        } else if (t->type == TOKEN_PIPE) {
            if (current_cmd.argc == 0) {
                shell_error("syntax error near unexpected token '|'");
                syntax_error = 1;
                break;
            }
            if (pipeline->num_commands >= cmd_capacity) {
                cmd_capacity *= 2;
                pipeline->commands = safe_realloc(pipeline->commands, sizeof(Command) * cmd_capacity);
            }
            pipeline->commands[pipeline->num_commands++] = current_cmd;
            cmd_init(&current_cmd);
            idx++;
            /* Check if trailing pipe */
            if (idx >= tl.count || tl.tokens[idx].type == TOKEN_EOF) {
                shell_error("syntax error: unexpected end of file after '|'");
                syntax_error = 1;
                break;
            }
        } else if (t->type == TOKEN_BACKGROUND) {
            /* Background token '&' should be at the end */
            idx++;
            if (idx < tl.count && tl.tokens[idx].type != TOKEN_EOF) {
                shell_error("syntax error near unexpected token '&'");
                syntax_error = 1;
                break;
            }
            pipeline->background = 1;
            break;
        } else {
            shell_error("syntax error: unexpected token");
            syntax_error = 1;
            break;
        }
    }

    if (!syntax_error) {
        if (current_cmd.argc > 0) {
            if (pipeline->num_commands >= cmd_capacity) {
                cmd_capacity += 1;
                pipeline->commands = safe_realloc(pipeline->commands, sizeof(Command) * cmd_capacity);
            }
            pipeline->commands[pipeline->num_commands++] = current_cmd;
        } else if (pipeline->num_commands > 0) {
            shell_error("syntax error: unexpected end of file after '|'");
            syntax_error = 1;
        } else {
            syntax_error = 1;
        }
    }

    token_list_free(&tl);

    if (syntax_error) {
        free_command(&current_cmd);
        free_pipeline(pipeline);
        return NULL;
    }

    return pipeline;
}
