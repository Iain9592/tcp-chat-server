CC = cc
CFLAGS = -Wall -Wextra -Werror -std=c11 -O2

.PHONY: all clean

all: server client

server: server.c server_utils.c protocol_server.c server_utils.h protocol_server.h
	$(CC) $(CFLAGS) server.c server_utils.c protocol_server.c -o server

client: client.c
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client *.o
