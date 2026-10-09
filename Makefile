CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra

PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin
UNITDIR    ?= /etc/systemd/system
SERVICE    := boot-display.service

boot_display: boot_display.c
	$(CC) $(CFLAGS) -o $@ $<

install: boot_display
	install -D -m 755 boot_display $(DESTDIR)$(BINDIR)/boot_display
	install -d $(DESTDIR)$(UNITDIR)
	sed 's|@BINDIR@|$(BINDIR)|g' $(SERVICE) > $(DESTDIR)$(UNITDIR)/$(SERVICE)
	chmod 644 $(DESTDIR)$(UNITDIR)/$(SERVICE)
	grep -qsx 'i2c-dev' /etc/modules /etc/modules-load.d/*.conf || { \
		install -d $(DESTDIR)/etc/modules-load.d && \
		echo i2c-dev > $(DESTDIR)/etc/modules-load.d/i2c-dev.conf; }
ifeq ($(DESTDIR),)
	systemctl daemon-reload
	systemctl enable $(SERVICE)
endif

uninstall:
ifeq ($(DESTDIR),)
	-systemctl disable $(SERVICE)
endif
	rm -f $(DESTDIR)$(UNITDIR)/$(SERVICE)
	rm -f $(DESTDIR)$(BINDIR)/boot_display
ifeq ($(DESTDIR),)
	systemctl daemon-reload
endif

clean:
	rm -f boot_display

.PHONY: install uninstall clean
