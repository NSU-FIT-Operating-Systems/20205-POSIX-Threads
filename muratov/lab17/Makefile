CC=gcc
CFLAGS=-c -Wall -Wextra -Werror -Wpedantic

FILENAME=reader
LIST=list

all: $(FILENAME).out

$(FILENAME).out: $(FILENAME).o $(LIST).o
	$(CC) -pthread $(FILENAME).o $(LIST).o -o $(FILENAME).out -lm

$(FILENAME).o: $(FILENAME).c
	$(CC) $(CFLAGS) $(FILENAME).c -o $(FILENAME).o

$(LIST).o: $(LIST).c
	$(CC) $(CFLAGS) $(LIST).c -o $(LIST).o

clean:
	rm -rf *.o
