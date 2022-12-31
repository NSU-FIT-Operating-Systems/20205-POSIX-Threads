all: server client

server: server.cpp
	clang++ server.cpp -Wall -fsanitize=memory -o server
client: client.c
	clang client.c -Wall -o client
clean:
	rm -f server client
