# Pull-mode decoding demo.
# Compiles the repository sources directly (no installed library, no new deps).
CFLAGS=-Wall -O2 -g -D__LINUX__
LIBS =

all: pull_decode

pull_decode: main.o
	$(CC) main.o $(LIBS) -o pull_decode

main.o: main.cpp ../../src/png.inl ../../src/PNGdec.cpp ../../src/PNGdec.h
	$(CXX) $(CFLAGS) -c main.cpp

clean:
	rm -f *.o pull_decode
