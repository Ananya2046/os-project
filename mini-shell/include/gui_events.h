/**
 * gui_events.h - Non-blocking FIFO IPC event emitter for Mini Linux Shell GUI
 * 
 * Course: OS Lab (24B11CS312), JIIT Noida
 */

#ifndef GUI_EVENTS_H
#define GUI_EVENTS_H

#include <sys/types.h>

/**
 * Initializes the GUI event emitter on the specified FIFO path.
 * Opens the FIFO with O_RDWR | O_NONBLOCK to guarantee non-blocking writes
 * and avoid blocking if the GUI is not yet reading.
 */
void gui_events_init(const char *fifo_path);

/**
 * Cleans up and closes the FIFO file descriptor.
 */
void gui_events_cleanup(void);

/**
 * Emits a structured event message to the FIFO channel.
 * Format: EVENT_TYPE|param1|param2|...
 * Non-blocking: returns immediately and safely handles broken pipes.
 */
void gui_event_emit(const char *fmt, ...);

#endif /* GUI_EVENTS_H */

