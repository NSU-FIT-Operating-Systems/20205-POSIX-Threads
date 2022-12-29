CC=gcc
CFLAGS=-c -Wall -Wextra

FILENAME=prog

all: $(FILENAME)

$(FILENAME): $(FILENAME).o
	$(CC) -pthread $(FILENAME).o -o $(FILENAME).out -lm

$(FILENAME).o: $(FILENAME).c
	$(CC) $(CFLAGS) $(FILENAME).c -o $(FILENAME).o

clean:
	rm -rf *.o
