# LBE-1420 config utility
# Original: simontheu - https://github.com/simontheu/lbe-1420
# Modificaciones: Copyright (c) 2026 Manuel - EA7JRS

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -Wall -Wextra -std=c11
PREFIX  ?= /usr/local
PROG     = lbe-142x
RULES    = 99-lbe-142x.rules

all: $(PROG)

$(PROG): lbe-142x.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: $(PROG)
	install -Dm755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)

install-udev:
	install -Dm644 $(RULES) $(DESTDIR)/etc/udev/rules.d/$(RULES)
	-udevadm control --reload-rules && udevadm trigger --subsystem-match=hidraw

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROG) $(DESTDIR)/etc/udev/rules.d/$(RULES)

clean:
	rm -f $(PROG)

.PHONY: all install install-udev uninstall clean
