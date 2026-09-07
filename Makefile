CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PKGS     = gtk+-3.0 webkit2gtk-4.1
CPPFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS))

.PHONY: all clean test

all: mdlive test_render

mdlive: mdlive.c render.c render.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ mdlive.c render.c $(LDLIBS)

test_render: test_render.c render.c render.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags glib-2.0) \
		-o $@ test_render.c render.c $(shell pkg-config --libs glib-2.0)

test: test_render
	./test_render sample.md

clean:
	rm -f mdlive test_render
