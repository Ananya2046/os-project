# Top-level Makefile forwarding to mini-shell/
# Course: OS Lab (24B11CS312), JIIT Noida

.PHONY: all clean test valgrind gui

all:
	$(MAKE) -C mini-shell all

clean:
	$(MAKE) -C mini-shell clean

test:
	$(MAKE) -C mini-shell test

valgrind:
	$(MAKE) -C mini-shell valgrind

gui:
	$(MAKE) -C mini-shell gui

