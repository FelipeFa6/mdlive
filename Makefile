CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PKGS    := gtk+-3.0 webkit2gtk-4.1 md4c-html
CFLAGS  += -std=c99 -Wall -Wextra $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))

PREFIX = /usr/local

.PHONY: all install uninstall clean

all: mdlive

mdlive: mdlive.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f mdlive ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/mdlive

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/mdlive

clean:
	rm -f mdlive
