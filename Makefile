CC = g++
RM = rm -f
CFLAGS = -O3 -Wall

HEADERS = object_file.h instructions.h

.cpp.o:	$(HEADERS) $<
	$(CC) $(CFLAGS) -c $<

all: wasm wlink

wasm: assembler.o instructions.o
	$(CC) $(CFLAGS) assembler.o instructions.o -o wasm

wlink: linker.o instructions.o
	$(CC) $(CFLAGS) linker.o instructions.o -o wlink

clean:
	$(RM) *.o *~ core wasm wlink wasm.exe wlink.exe
