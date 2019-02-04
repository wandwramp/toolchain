CC = g++
RM = rm -f
CFLAGS = -std=c++98 -O3 -Wall -Wno-write-strings -g
ifndef INSTALLDIR
INSTALLDIR=~/wramp-install/
endif
MKDIR=mkdir -p
COPY=cp
BUILDBINS=wasm wlink wobj
INSTALLBINS=$(INSTALLDIR)wasm $(INSTALLDIR)wlink $(INSTALLDIR)wobj
HEADERS = object_file.h instructions.h

.cpp.o:	$(HEADERS) $<
	$(CC) $(CFLAGS) -c $<

all: wasm wlink wobj

wasm: assembler.o instructions.o
	$(CC) $(CFLAGS) assembler.o instructions.o -o wasm

wlink: linker.o instructions.o
	$(CC) $(CFLAGS) linker.o instructions.o -o wlink

wobj: objectViewer.o instructions.o
	$(CC) $(CFLAGS) objectViewer.o instructions.o -o wobj

clean:
	$(RM) *.o *~

clobber:
	$(RM) wasm wlink
	$(RM) $(INSTALLBINS)

install: all
	$(MKDIR) $(INSTALLDIR)
	$(COPY) $(BUILDBINS) $(INSTALLDIR)
