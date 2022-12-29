all: server client

server: server.cpp
	clang++ server.cpp -Wall -o server
client: client.c
	clang client.c -Wall -o client
clean:
	rm -f server client
