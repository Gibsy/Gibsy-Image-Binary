CC      = gcc
CFLAGS  = -O2 -Wall -std=c11
LIBS_BASE = -lzstd -lpng -lm
LIBS_SDL  = $(shell sdl2-config --cflags --libs 2>/dev/null || echo "-lSDL2")

.PHONY: all clean

all: img2gib gib_viewer

img2gib: img2gib.c gib.h
	$(CC) $(CFLAGS) -o img2gib img2gib.c $(LIBS_BASE) -ljpeg
	@echo "  OK  img2gib"

gib_viewer: gib_viewer.c gib.h
	$(CC) $(CFLAGS) $(shell sdl2-config --cflags 2>/dev/null) \
	      -o gib_viewer gib_viewer.c $(LIBS_BASE) $(LIBS_SDL)
	@echo "  OK  gib_viewer"

clean:
	rm -f img2gib gib_viewer