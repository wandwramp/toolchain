CC = g++
RM = rm -f
CFLAGS = -std=c++98 -O3 -Wall -Wno-write-strings -g

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
