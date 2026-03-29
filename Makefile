CC ?= gcc
CFLAGS ?= -O3 -Wall

all: aigrandom

aiger.o: aiger.c aiger.h
	$(CC) $(CFLAGS) -c aiger.c

aigrandom.o: aigrandom.c aigrandom.h aiger.h
	$(CC) $(CFLAGS) -c aigrandom.c

aigrandom: aigrandom.o aiger.o aigrandom.c aigrandom.h aiger.h
	$(CC) $(CFLAGS) -o $@ aigrandom.o aiger.o

libaigrandom.a: aigrandom.o aiger.o
	ar rcs $@ $^

clean:
	rm -f aigrandom aigrandom.o aiger.o libaigrandom.a

.PHONY: all clean libaigrandom.a
