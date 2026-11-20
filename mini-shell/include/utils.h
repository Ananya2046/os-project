/**
 * utils.h - Utility functions and common definitions for Mini Linux Shell
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#ifndef UTILS_H
#define UTILS_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* Safe memory allocation helpers that abort on out-of-memory */
void *safe_malloc(size_t size);
void *safe_realloc(void *ptr, size_t size);
char *safe_strdup(const char *s);

/* String manipulation helpers */
char *trim_whitespace(char *str);
int is_whitespace(char c);
void resolve_backspaces(char *str);

/* Error reporting helpers */
void shell_error(const char *msg);
void shell_perror(const char *msg);

#endif /* UTILS_H */

