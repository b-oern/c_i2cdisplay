CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra

boot_display: boot_display.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f boot_display

.PHONY: clean
