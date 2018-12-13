CC = g++
RM = rm -f
CFLAGS = -std=c++98 -O3 -Wall -Wno-write-strings -g
ifndef INSTALLDIR
INSTALLDIR=~/wramp-install/
endif
MKDIR=mkdir -p
COPY=cp
BUILDBINS=wasm wlink 
INSTALLBINS=$(INSTALLDIR)wasm $(INSTALLDIR)wlink
HEADERS = object_file.h instructions.h

.cpp.o:	$(HEADERS) $<
	$(CC) $(CFLAGS) -c $<

all: wasm wlink

wasm: assembler.o instructions.o
	$(CC) $(CFLAGS) assembler.o instructions.o -o wasm

wlink: linker.o instructions.o
	$(CC) $(CFLAGS) linker.o instructions.o -o wlink

clean:
	$(RM) *.o *~

clobber:
	$(RM) wasm wlink
	$(RM) $(INSTALLBINS)

install: all
	$(MKDIR) $(INSTALLDIR)
	$(COPY) $(BUILDBINS) $(INSTALLDIR)
