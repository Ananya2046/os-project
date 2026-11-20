/**
 * gui_events.c - Implementation of non-blocking FIFO event emitter for GUI
 * 
 * OS Concept: Named Pipes (FIFOs) for Local Inter-Process Communication
 * Communicates kernel and shell state changes (job transitions, signals,
 * process creation, pipelines) asynchronously to the GUI frontend.
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#include "gui_events.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

static int g_fifo_fd = -1;

void gui_events_init(const char *fifo_path) {
    if (!fifo_path || *fifo_path == '\0') return;

    if (g_fifo_fd >= 0) {
        close(g_fifo_fd);
        g_fifo_fd = -1;
    }

    /*
     * Open FIFO with O_RDWR | O_NONBLOCK:
     * - O_RDWR prevents open() from blocking waiting for a reader.
     * - O_NONBLOCK guarantees write() calls never hang the shell.
     */
    g_fifo_fd = open(fifo_path, O_RDWR | O_NONBLOCK);
    if (g_fifo_fd >= 0) {
        /* Ignore SIGPIPE so writing to a closed GUI FIFO never terminates the shell */
        signal(SIGPIPE, SIG_IGN);
    }
}

void gui_events_cleanup(void) {
    if (g_fifo_fd >= 0) {
        close(g_fifo_fd);
        g_fifo_fd = -1;
    }
}

void gui_event_emit(const char *fmt, ...) {
    if (g_fifo_fd < 0 || !fmt) return;

    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, sizeof(buffer) - 2, fmt, args);
    va_end(args);

    if (len <= 0) return;

    /* Ensure exactly one trailing newline */
    if (buffer[len - 1] != '\n') {
        buffer[len++] = '\n';
        buffer[len] = '\0';
    }

    /* Non-blocking write: write what is available; ignore errors if GUI closed */
    ssize_t written = write(g_fifo_fd, buffer, len);
    (void)written;
}

