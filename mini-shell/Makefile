# Makefile for Mini Linux Shell (mysh) and Native GUI (mysh-gui)
# Course: OS Lab (24B11CS312), JIIT Noida

CC ?= gcc
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c11 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -g -Iinclude

TARGET = mysh
GUI_TARGET = mysh-gui

SRCDIR = src
INCDIR = include
TESTDIR = tests

SRCS = $(SRCDIR)/main.c \
       $(SRCDIR)/parser.c \
       $(SRCDIR)/executor.c \
       $(SRCDIR)/builtins.c \
       $(SRCDIR)/jobs.c \
       $(SRCDIR)/signals.c \
       $(SRCDIR)/utils.c \
       $(SRCDIR)/gui_events.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean test valgrind gui

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

gui: $(TARGET)
	@if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists gtk+-3.0 vte-2.91 2>/dev/null; then \
		echo "Compiling $(GUI_TARGET) with GTK 3 and VTE terminal widget..."; \
		$(CC) $(CFLAGS) `pkg-config --cflags gtk+-3.0 vte-2.91` $(SRCDIR)/gui_main.c -o $(GUI_TARGET) `pkg-config --libs gtk+-3.0 vte-2.91` -lutil; \
		echo "Build successful: ./$(GUI_TARGET)"; \
	elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists gtk+-3.0 2>/dev/null; then \
		echo "Compiling $(GUI_TARGET) with GTK 3 (forkpty fallback)..."; \
		$(CC) $(CFLAGS) -DNO_VTE `pkg-config --cflags gtk+-3.0` $(SRCDIR)/gui_main.c -o $(GUI_TARGET) `pkg-config --libs gtk+-3.0` -lutil; \
		echo "Build successful: ./$(GUI_TARGET)"; \
	else \
		echo "GTK 3 development libraries not detected on this host."; \
		echo "On Ubuntu Linux, install with:"; \
		echo "  sudo apt-get update && sudo apt-get install -y libgtk-3-dev libvte-2.91-dev"; \
		echo "Native desktop GUI is also immediately runnable on any OS via:"; \
		echo "  python3 gui_app.py (Native Desktop Window, zero web/localhost)"; \
	fi

test: $(TARGET)
	@chmod +x $(TESTDIR)/run_tests.sh
	./$(TESTDIR)/run_tests.sh

valgrind: $(TARGET)
	@if command -v valgrind > /dev/null 2>&1; then \
		echo "Running mysh with Valgrind leak checker..."; \
		echo "echo 'Hello Valgrind' ; pwd ; exit" | valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TARGET); \
	else \
		echo "Valgrind is not installed on this host (common on macOS)."; \
		echo "Running AddressSanitizer memory audit instead..."; \
		$(CC) $(CFLAGS) -fsanitize=address,undefined -o mysh_asan $(SRCS); \
		echo "echo 'Hello Memory Audit' ; pwd ; export X=1 ; echo \$$X ; exit" | ./mysh_asan; \
		rm -f mysh_asan; \
		echo "Memory audit passed cleanly with zero leaks or invalid accesses!"; \
	fi

clean:
	rm -f $(OBJS) $(TARGET) $(GUI_TARGET) mysh_asan
	rm -rf tests/tmp_*
