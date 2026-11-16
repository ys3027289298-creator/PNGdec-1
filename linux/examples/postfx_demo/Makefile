CFLAGS=-Wall -O2 -g
LIBS = -lm

all: postfx_demo

postfx_demo: main.o
	$(CC) main.o $(LIBS) -o postfx_demo

main.o: main.cpp ../../../src/png.inl ../../../src/PNGdec.cpp
	$(CXX) $(CFLAGS) -c main.cpp

clean:
	rm -f *.o postfx_demo
