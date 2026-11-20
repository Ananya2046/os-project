/**
 * utils.c - Implementation of utility functions for Mini Linux Shell
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#include "utils.h"
#include <ctype.h>

void *safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr && size > 0) {
        fprintf(stderr, "mysh: fatal error: out of memory (malloc %zu bytes)\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *safe_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size > 0) {
        fprintf(stderr, "mysh: fatal error: out of memory (realloc %zu bytes)\n", size);
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

char *safe_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = safe_malloc(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

char *trim_whitespace(char *str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) {
        str++;
    }
    if (*str == '\0') {
        return str;
    }
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return str;
}

int is_whitespace(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

void resolve_backspaces(char *str) {
    if (!str) return;
    int r = 0, w = 0;
    while (str[r] != '\0') {
        if (str[r] == 0x7f || str[r] == 0x08) {
            if (w > 0) {
                w--;
            }
        } else {
            str[w++] = str[r];
        }
        r++;
    }
    str[w] = '\0';
}

void shell_error(const char *msg) {
    if (msg) {
        fprintf(stderr, "mysh: %s\n", msg);
    }
}

void shell_perror(const char *msg) {
    if (msg) {
        fprintf(stderr, "mysh: %s: %s\n", msg, strerror(errno));
    } else {
        fprintf(stderr, "mysh: %s\n", strerror(errno));
    }
}

