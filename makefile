all: first second

first: client.c
	gcc -g client.c -o WTF

second: server.c
	gcc -g -pthread new_server.c -o new_server

clean:
	rm -rf WTF server
